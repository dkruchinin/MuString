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
 * (c) Copyright 2006,2007,2008 MString Core Team <http://mstring.jarios.org>
 * (c) Copyright 2008 Michael Tsymbalyuk <mtzaurus@gmail.com>
 *
 * ipc/ipc_poll.c: implementation of the IPC ports polling mechanism.
 *
 */

#include <eza/arch/types.h>
#include <kernel/syscalls.h>
#include <ipc/ipc.h>
#include <eza/task.h>
#include <eza/smp.h>
#include <ipc/poll.h>
#include <eza/time.h>
#include <mm/slab.h>
#include <eza/errno.h>
#include <mm/pfalloc.h>
#include <eza/waitqueue.h>
#include <eza/arch/page.h>
#include <eza/scheduler.h>
#include <eza/usercopy.h>
#include <eza/arch/atomic.h>
#include <ipc/port.h>

typedef struct __poll_kitem {
  wqueue_task_t qtask;
  ipc_gen_port_t *port;
  poll_event_t events,revents;
  bool queued;
} poll_kitem_t;

struct __plazy_data {
  ulong_t nqueues;
  atomic_t wq_stat;
};

static bool poll_deferred_sched_handler(void *data)
{
  struct __plazy_data *p=(struct __plazy_data*)data;
  return atomic_get(&p->wq_stat)==p->nqueues;
}

long sys_ipc_port_poll(pollfd_t *pfds,ulong_t nfds,timeval_t *timeout)
{
  long nevents;
  task_t *caller=current_task();
  ulong_t size=nfds*sizeof(poll_kitem_t);
  poll_kitem_t *pkitems;
  bool use_slab=(size<=512);
  ulong_t i;
  struct __plazy_data ldata;

  if( !pfds || !nfds || !caller->ipc || nfds > MAX_POLL_OBJECTS ) {
    return -EINVAL;
  }

  if( !valid_user_address_range((uintptr_t)pfds,nfds*sizeof(pollfd_t)) ) {
    return -EFAULT;
  }

  /* TODO: [mt] Add process memory limit check. [R] */
  if( use_slab ) {
    pkitems=memalloc(size);
    memset(pkitems,0,size);
  } else {
    pkitems=alloc_pages_addr((size>>PAGE_WIDTH)+1,AF_ZERO);
  }

  if( !pkitems ) {
    return -ENOMEM;
  }

  nevents=0;
  atomic_set(&ldata.wq_stat,0);
  ldata.nqueues=0;

  /* First, create the list of all IPC ports to be polled. */
  for(i=0;i<nfds;i++) {
    pollfd_t upfd;
    ipc_gen_port_t *port;

    if( copy_from_user(&upfd,&pfds[i],sizeof(upfd)) ) {
      nevents=-EFAULT;
      break;
    }

    port=ipc_get_port(caller,upfd.fd);
    if( !port ) {
      nevents=-EINVAL;
      break;
    }

    pkitems[i].port=port;
    pkitems[i].events=upfd.events;

    waitqueue_prepare_task(&pkitems[i].qtask,current_task());
    pkitems[i].qtask.wq_stat=&ldata.wq_stat;

    pkitems[i].revents=ipc_port_check_events(port,&pkitems[i].qtask,
                                             upfd.events);
    pkitems[i].queued=pkitems[i].revents==0;
    if( pkitems[i].revents ) {
      nevents++;
    } else {
      ldata.nqueues++;
    }
  }

  if( nevents < 0 ) { /* Failure during processing user data, so roll back. */
    goto put_ports;
  } else if(nevents > 0) { /* Some ports have pending events,so don't sleep. */
    goto put_ports;
  }

  /* Sleep a little bit. */
  sched_change_task_state_deferred(caller,TASK_STATE_SLEEPING,
                                  poll_deferred_sched_handler,&ldata);

  /* Count the resulting number of pending events. */
  nevents=0;
  for(i=0;i<nfds;i++) {
    pkitems[i].revents=ipc_port_check_events(pkitems[i].port,NULL,pkitems[i].events);
    if( pkitems[i].revents ) {
      nevents++;
    }
  }

put_ports:
  /* Process pending events. */
  for(i=0;i<nfds && pkitems[i].port;i++) {
    if( pkitems[i].queued ) {
      ipc_port_remove_poller(pkitems[i].port,&pkitems[i].qtask);
    }
    ipc_put_port(pkitems[i].port);

    /* Copy resulting events back to userspace. */
    if( nevents > 0 ) {
      if(copy_to_user(&pfds[i].revents,&pkitems[i].revents,
                      sizeof(poll_event_t))) {
        nevents=-EFAULT;
      }
    }
  }

  /* Free buffers. */
  if( use_slab ) {
    memfree(pkitems);
  } else {
    free_pages_addr(pkitems, (size>>PAGE_WIDTH)+1);
  }
  return nevents;
}
