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
 * (c) Copyright 2008 Dan Kruchinin <dan.kruchinin@gmail.com>
 *
 * mlibc/waitqueue.c: implementation of waitqueues.
 */

#include <ds/list.h>
#include <ds/waitqueue.h>
#include <eza/task.h>
#include <eza/spinlock.h>
#include <eza/scheduler.h>
#include <eza/arch/types.h>

static void __insert_task(wait_queue_t *wq, wait_queue_task_t *wq_task)
{
  list_node_t *next = NULL;
  
  if (likely(!list_is_empty(&wq->waiters))) {
    wait_queue_task_t *t;
    
    list_for_each_entry(&wq->waiters, t, node) {
      if (t->priority == wq_task->priority) {
        next = list_head(&t->head);
        break;
      }
      if (t->priority > wq_task->priority) {
        next = &t->node;
        break;
      }
    }
  }
  else
    next = list_head(&wq->waiters);

  list_add(next, &wq_task->node);
  wq_task->q = wq;
  wq->num_waiters++;
}

static void __delete_task(wait_queue_t *wq, wait_queue_task_t *wq_task)
{    
  if (!list_is_empty(&wq_task->head)) {
    wait_queue_task_t *t = list_entry(list_node_first(&wq_task->head),
                                      wait_queue_task_t, node);
    list_del(&t->node);
    list_add(&wq_task->node, &t->node);
    if (!list_is_empty(&wq_task->head))
      list_move(list_head(&t->head), list_head(&t->head), &wq_task->head);
  }

  list_del(&wq_task->node);
  list_init_head(&wq_task->head);
  wq_task->q = NULL;
  wq->num_waiters--;
}

status_t waitqueue_initialize(wait_queue_t *wq)
{
  list_init_head(&wq->waiters);
  wq->num_waiters = 0;  
  spinlock_initialize(&wq->q_lock);
  return 0;
}

status_t waitqueue_prepare_task(wait_queue_task_t *wq_task, task_t *task)
{
  status_t ret;
  
  list_init_head(&wq_task->head);
  wq_task->task = task;
  wq_task->q = NULL;
  ret = do_scheduler_control(task, SYS_SCHED_CTL_GET_PRIORITY, 0);
  if (ret < 0)
    return ret;

  wq_task->priority = ret;
  return 0;
}

status_t waitqueue_insert(wait_queue_t *wq, wait_queue_task_t *wq_task, wqueue_insop_t iop)
{
  status_t ret = 0;
  
  spinlock_lock(&wq->q_lock);
  __insert_task(wq, wq_task);
  if (iop == WQ_INSERT_SLEEP)
    ret = sched_change_task_state(wq_task->task, TASK_STATE_SLEEPING);

  spinlock_unlock(&wq->q_lock);
  return ret;
}

status_t waitqueue_delete(wait_queue_t *wq, wait_queue_task_t *wq_task, wqueue_delop_t dop)
{
  status_t ret = 0;

  if (waitqueue_is_empty(wq)) {
    ret = -EINVAL;
    goto out;
  }
  
  spinlock_lock(&wq->q_lock);
  __delete_task(wq, wq_task);
  if (dop == WQ_DELETE_WAKEUP)
    ret = sched_change_task_state(wq_task->task, TASK_STATE_RUNNABLE);

  spinlock_unlock(&wq->q_lock);

  out:
  return ret;
}

wait_queue_task_t *waitqueue_first_task(wait_queue_t *wq)
{
  wait_queue_task_t *t = NULL;
  
  if (!waitqueue_is_empty(wq)) {
    spinlock_lock(&wq->q_lock);
    t = list_entry(list_node_first(&wq->waiters), wait_queue_task_t, node);
    spinlock_unlock(&wq->q_lock);
  }

  return t;
}

#ifdef DEBUG_WAITQUEUE
#include <mlibc/kprintf.h>

void waitqueue_dump(wait_queue_t *wq)
{
  if (waitqueue_is_empty(wq)) {
    kprintf("[WQ empty]\n");
    return;
  }
  else {
    wait_queue_task_t *t;

    spinlock_lock(&wq->q_lock);
    kprintf("[WQ dump (%d waiters)]:\n", wq->num_waiters);
    list_for_each_entry(&wq->waiters, t, node) {
      wait_queue_task_t *subt;
      
      kprintf(" [%d]=> %d", t->priority, t->task->pid);
      list_for_each_entry(&t->head, subt, node)
        kprintf("->%d", subt->task->pid);

      kprintf("\n");
    }
    
    spinlock_unlock(&wq->q_lock);
  }
}
#endif /* DEBUG_WAITQUEUE */
