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
 * mlibc/waitqueue.c: implementation of waitqueues.
 */

#include <eza/task.h>
#include <eza/spinlock.h>
#include <ds/list.h>
#include <eza/arch/interrupt.h>
#include <eza/arch/types.h>
#include <eza/scheduler.h>
#include <eza/waitqueue.h>

void waitqueue_initialize(wait_queue_t *wq)
{
  list_init_head(&wq->waiters);
  wq->num_waiters=0;
  spinlock_initialize(&wq->q_lock,"");
}

void waitqueue_add_task(wait_queue_t *wq,wait_queue_task_t *w)
{
  list_init_node(&w->l);
  w->flags = 0;

  LOCK_WAITQUEUE(wq);
  list_add2tail(&wq->waiters,&w->l);
  wq->num_waiters++;
  UNLOCK_WAITQUEUE(wq);
}

bool waitqueue_is_empty(wait_queue_t *wq)
{
  /* No need to lock - we just read a pointer. */
  return list_is_empty(&wq->waiters);
}

ulong_t waitqueue_get_tasks_number(wait_queue_t *wq)
{
  return wq->num_waiters;
}

void waitqueue_wake_one_task(wait_queue_t *wq)
{
  LOCK_WAITQUEUE(wq);
  if(wq->num_waiters > 0) {
    wait_queue_task_t *waiter;
    list_node_t *n = list_node_first(&wq->waiters);

    wq->num_waiters--;
    list_del(n);
    UNLOCK_WAITQUEUE(wq);

    waiter = container_of(n,wait_queue_task_t,l);
    waiter->flags |= WQ_EVENT_OCCURED;
    sched_change_task_state(waiter->task,TASK_STATE_RUNNABLE);
  }
}

static bool waitqueue_lazy_sched_handler(void *data)
{
  wait_queue_task_t *t = (wait_queue_task_t*)data;
  return !(t->flags & WQ_EVENT_OCCURED);
}

void waitqueue_yield(wait_queue_task_t *w)
{
  sched_change_task_state_lazy(w->task,TASK_STATE_SLEEPING,
                               waitqueue_lazy_sched_handler,w);
}


