/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * (c) Copyright 2006,2007,2008 MString Core Team <http://mstring.berlios.de>
 * (c) Copyright 2008 Michael Tsymbalyuk <mtzaurus@gmail.com>
 *
 * ipc/ipc_poll.c: implementation of the IPC ports polling mechanism.
 *
 */

#include <eza/arch/types.h>
#include <kernel/syscalls.h>
#include <kernel/vm.h>
#include <ipc/ipc.h>
#include <eza/task.h>
#include <eza/smp.h>
#include <eza/waitqueue.h>
#include <ipc/poll.h>
#include <eza/time.h>
#include <mm/slab.h>
#include <eza/waitqueue.h>
#include <eza/errno.h>
#include <mm/pfalloc.h>
#include <eza/arch/page.h>
#include <eza/scheduler.h>
#include <eza/arch/atomic.h>

typedef struct __poll_kitem {
  wait_queue_task_t qtask;
  ipc_port_t *port;
  poll_event_t events,revents;
} poll_kitem_t;

struct __plazy_data {
  task_t *task;
  ulong_t nqueues;
};

static bool poll_lazy_sched_handler(void *data)
{
  struct __plazy_data *p=(struct __plazy_data*)data;
  return atomic_get(&p->task->ipc->pstats->active_queues)==p->nqueues;
}

status_t sys_ipc_port_poll(pollfd_t *pfds,ulong_t nfds,timeval_t *timeout)
{
  status_t nevents;
  task_t *caller=current_task();
  ulong_t size=nfds*sizeof(wait_queue_task_t);
  poll_kitem_t *pkitems;
  bool use_slab=(size<=512);
  ulong_t i;
  bool caller_was_added;
  struct __plazy_data ldata;

  if( !pfds || !nfds || !caller->ipc || nfds > MAX_POLL_OBJECTS ) {
    return -EINVAL;
  }

//  if( !valid_user_address_range(pfds,nfds*sizeof(pollfd_t)) ) {
//    return -EFAULT;
//  }

  /* TODO: [mt] Add process memory limit check. [R] */
  if( use_slab ) {
    pkitems=memalloc(size);
    memset(pkitems,0,size);
  } else {
    pkitems=alloc_pages_addr((size>>PAGE_WIDTH)+1,AF_PGEN|AF_ZERO);
  }

  if( !pkitems ) {
    return -ENOMEM;
  }

  nevents=0;
  caller_was_added=false;

  /* First, create the list of all IPC ports to be polled. */
  for(i=0;i<nfds;i++) {
    pollfd_t upfd;
    ipc_port_t *port;

    if( copy_from_user(&upfd,&pfds[i],sizeof(upfd)) ) {
      nevents=-EFAULT;
      break;
    }

    if( ipc_get_port(caller,upfd.fd,&port) ) {
      nevents=-EINVAL;
      break;
    }

    pkitems[i].port=port;
    pkitems[i].events=upfd.events;
    pkitems[i].revents=ipc_port_get_pending_events(port) & upfd.events;

    if( pkitems[i].revents ) {
      nevents++;
    }
  }

  if( nevents < 0 ) { /* Failure during processing user data, so roll back. */
    goto put_ports;
  } else if(nevents > 0) { /* Some ports have pending events,so don't sleep. */
    goto put_ports;
  }

  /* No pending events found, so add caller to waitqueues. */
  for(i=0;i<nfds;i++) {
    IPC_TASK_ACCT_OPERATION(caller);
    ipc_port_add_poller(pkitems[i].port,caller,&pkitems[i].qtask);
  }
  caller_was_added=true;

  /* Sleep a little bit. */
  ldata.task=caller;
  ldata.nqueues=nfds;
  sched_change_task_state_lazy(caller,TASK_STATE_SLEEPING,
                               poll_lazy_sched_handler,&ldata);

  /* Count the resulting number of pending events. */
  nevents=0;
  for(i=0;i<nfds;i++) {
    pkitems[i].revents=ipc_port_get_pending_events(pkitems[i].port) &
                                                   pkitems[i].events;
    if( pkitems[i].revents ) {
      nevents++;
    }
  }

put_ports:
  /* Process pending events. */
  for(i=0;i<nfds && pkitems[i].port;i++) {
    if(caller_was_added) {
      ipc_port_remove_poller(pkitems[i].port,&pkitems[i].qtask);
      IPC_TASK_UNACCT_OPERATION(caller);
    }
    ipc_put_port(pkitems[i].port);

    /* Copy resulting events back to userspace. */
    if(nevents>0) {
      if(copy_to_user(&pfds[i].revents,&pkitems[i].revents,
                      sizeof(poll_event_t))) {
        nevents=-EFAULT;
      }
    }
  }

  /* Free buffers. */
  if(use_slab) {
    memfree(pkitems);
  } else {
    free_pages_addr(pkitems);
  }
  return nevents;
}
