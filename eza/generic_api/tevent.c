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
 * eza/generic_api/tevent.c: implementation of functions related to task events.
 */

#include <eza/task.h>
#include <eza/arch/types.h>
#include <kernel/syscalls.h>
#include <eza/spinlock.h>
#include <eza/tevent.h>
#include <eza/errno.h>
#include <mm/slab.h>
#include <ds/list.h>
#include <eza/task.h>
#include <ipc/gen_port.h>
#include <ipc/ipc.h>

#define __free_listener(l)  memfree(l)

static task_event_listener_t *__alloc_listener(void)
{
  task_event_listener_t *l=memalloc(sizeof(*l));

  if( l ) {
    list_init_node(&l->owner_list);
    list_init_node(&l->llist);
    l->port=NULL;
  }

  return l;
}

void task_event_notify(ulong_t events)
{
  task_t *task=current_task();
  task_events_t *te=&task->task_events;

  LOCK_TASK_EVENTS_R(te);
  if( !list_is_empty(&task->task_events.listeners) ) {
    list_node_t *n;
    task_event_descr_t e;
    iovec_t iov;

    e.pid=task->pid;
    e.tid=task->tid;

    iov.iov_base=&e;
    iov.iov_len=sizeof(e);

    list_for_each(&task->task_events.listeners,n) {
      task_event_listener_t *l=container_of(n,task_event_listener_t,llist);

      if( l->events & events ) {
        e.ev_mask=l->events & events;
        ipc_port_message_t *msg=ipc_create_port_message_iov(&iov,1,sizeof(e),
                                                            false,0,0);

        if( msg ) {
          __ipc_port_send(l->port,msg,false,0,0);
        }
      }
    }
  }
  UNLOCK_TASK_EVENTS_R(te);
}

status_t task_event_attach(task_t *target,task_t *listener,
                           task_event_ctl_arg *ctl_arg)
{
  ipc_gen_port_t *port;
  task_event_listener_t *l;
  status_t r=-EINVAL;
  list_node_t *n;

  if( !ctl_arg->ev_mask || (ctl_arg->ev_mask & ~(ALL_TASK_EVENTS_MASK)) ) {
    return -EINVAL;
  }

  l=__alloc_listener();
  if( !l ) {
    return -ENOMEM;
  }

  if( !(port=__ipc_get_port(listener,ctl_arg->port)) ) {
    goto out_free;
  }

  if( port->flags & IPC_BLOCKED_ACCESS ) {
    goto put_port;
  }

  l->port=port;
  l->events=ctl_arg->ev_mask;
  l->listener=listener;

  /* Make sure caller hasn't installed another listenersfor this process. */
  LOCK_TASK_EVENTS_W(target);
  if( check_task_flags(target,TF_EXITING) ) {
    /* Target task became a zombie ? */
    r=-ESRCH;
    goto dont_add;
  }

  list_for_each(&target->task_events.listeners,n) {
    task_event_listener_t *tl=container_of(n,task_event_listener_t,llist);

    if( tl->listener == listener ) {
      r=-EBUSY;
      goto dont_add;
    }
  }
  list_add2tail(&target->task_events.listeners,&l->llist);
  r=0;
dont_add:
  UNLOCK_TASK_EVENTS_W(target);

  if( r ) {
    goto put_port;
  }

  /* Add this listener to our list. */
  LOCK_TASK_EVENTS_W(listener);
  list_add2tail(&listener->task_events.my_events,&l->owner_list);
  UNLOCK_TASK_EVENTS_W(listener);

  return r;
put_port:
  __ipc_put_port(port);
out_free:
  __free_listener(l);
  return r;
}

void exit_task_events(task_t *target)
{
  list_node_t *n,*ns;
  task_events_t *te=&target->task_events;

  /* Step 1: Remove our listeners. */
  while(1) {
    LOCK_TASK_EVENTS_W(te);
    if( !list_is_empty(&te->my_events) ) {
      n=list_node_first(&te->my_events);
      task_event_listener_t *l=container_of(n,task_event_listener_t,owner_list);
      list_del(n);
      UNLOCK_TASK_EVENTS_W(te);

      /* Now remove this events from target process's list. */
      LOCK_TASK_EVENTS_W(&l->listener->task_events);
      if( list_node_is_bound(&l->llist) ) {
        list_del(&l->llist);
      }
      UNLOCK_TASK_EVENTS_W(&l->listener->task_events);

      /* Free port and listener itself. */
      __ipc_put_port(l->port);
      __free_listener(l);
    } else {
      UNLOCK_TASK_EVENTS_W(te);
      break;
    }
  }

  /* Step 2: Remove listeners that target us. */
  LOCK_TASK_EVENTS_W(te);
  list_for_each_safe(&te->listeners,n,ns) {
    list_del(n);
  }
  UNLOCK_TASK_EVENTS_W(te);
}
