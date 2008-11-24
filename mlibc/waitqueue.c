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
 * (c) Copyright 2008 Dan Kruchinin <dan.kruchinin@gmail.com>
 *
 * mlibc/waitqueue.c: Implementation of waitqueues.
 */

#include <ds/list.h>
#include <ds/waitqueue.h>
#include <eza/task.h>
#include <eza/spinlock.h>
#include <eza/scheduler.h>
#include <eza/arch/types.h>

static void __insert_task(wqueue_t *wq, wqueue_task_t *wq_task)
{
  list_node_t *next = NULL;
  
  if (likely(!list_is_empty(&wq->waiters))) {
    wqueue_task_t *t;
    
    list_for_each_entry(&wq->waiters, t, node) {
      int cmp_ret = wq->cmp_func(t, wq_task);
      
      if (!cmp_ret) { /* t == wq_task */
        next = list_head(&t->head);
        break;
      }
      if (t > 0) { /* t > wq_task */
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

static void __delete_task(wqueue_task_t *wq_task)
{    
  if (!list_is_empty(&wq_task->head)) {
    wqueue_task_t *t = list_entry(list_node_first(&wq_task->head),
                                      wait_queue_task_t, node);
    list_del(&t->node);
    list_add(&wq_task->node, &t->node);
    if (!list_is_empty(&wq_task->head))
      list_move(list_head(&t->head), list_head(&t->head), &wq_task->head);
  }

  list_del(&wq_task->node);
  list_init_head(&wq_task->head);  
  wq_task->q->num_waiters--;
  wq_task->q = NULL;
}

status_t waitqueue_initialize(wqueue_t *wq, wqueue_type_t *type)
{  
  list_init_head(&wq->waiters);
  wq->num_waiters = 0;  
  spinlock_initialize(&wq->q_lock);
  wq->type = type;
  switch (type) {
      case WQ_PRIO:
        wq->cmp_func = __wq_tasks_prio_cmp;
        break;
      case WQ_CUSTOM:
        wq->cmp_func = NULL;
        break;
      default:
        return -EINVAL;
  }
  
  return 0;
}

void waitqueue_prepare_task(wqueue_task_t *wq_task, task_t *task)
{
  list_init_head(&wq_task->head);
  wq_task->task = task;
  wq_task->q = NULL;
  wq->task->uspc_blocked = task->flags & TF_USPC_BLOCKED;
  task->flags |= TF_USPC_BLOCKED; /* block priority changing from user-space */
}

status_t waitqueue_insert(wqueue_t *wq, wqueue_task_t *wq_task, wqueue_insop_t iop)
{
  status_t ret = 0;
  
  spinlock_lock(&wq->q_lock);
  ASSERT(wq->cmp_func != NULL);
  __insert_task(wq, wq_task);
  if (iop == WQ_INSERT_SLEEP)
    ret = sched_change_task_state(wq_task->task, TASK_STATE_SLEEPING);

  spinlock_unlock(&wq->q_lock);
  return ret;
}

status_t waitqueue_delete(wqueue_task_t *wq_task, wqueue_delop_t dop)
{
  status_t ret = 0;
  wqueue_t *wq = wq_task->q;

  spinlock_lock(&wq->q_lock);
  if (waitqueue_is_empty(wq)) {
    ret = -EINVAL;
    goto out;
  }
   
  __delete_task(wq_task);
  if (!wq_task->uspc_blocked)
    wq_task->task &= ~TF_USPC_BLOCKED;
  if (dop == WQ_DELETE_WAKEUP)
    ret = sched_change_task_state(wq_task->task, TASK_STATE_RUNNABLE);

  out:
  spinlock_unlock(&wq->q_lock);
  return ret;
}

wqueue_task_t *waitqueue_first_task(wait_queue_t *wq)
{
  wqueue_task_t *t = NULL;

  spinlock_lock(&wq->q_lock);
  if (!waitqueue_is_empty(wq))
    t = list_entry(list_node_first(&wq->waiters), wait_queue_task_t, node);    

  spinlock_unlock(&wq->q_lock);
  return t;
}

#ifdef DEBUG_WAITQUEUE
#include <mlibc/kprintf.h>

void waitqueue_dump(wqueue_t *wq)
{
  if (waitqueue_is_empty(wq)) {
    kprintf("[WQ empty]\n");
    return;
  }
  else {
    wait_queue_task_t *t;
    char *wq_type = NULL;

    spinlock_lock(&wq->q_lock);
    switch (wq->type) {
        case WQ_PRIO:
          wq_type = "Priority based";
          break;
        case WQ_CUSTOM:
          wq_type = "Custom";
          break;
    }
    
    kprintf("[WQ dump (TYPE: %s, Waiters: %d)]:\n", wq_type, wq->num_waiters);
    list_for_each_entry(&wq->waiters, t, node) {
      wqueue_task_t *subt;
      
      kprintf(" [%d]=> %d", t->priority, t->task->pid);
      list_for_each_entry(&t->head, subt, node)
        kprintf("->%d", subt->task->pid);

      kprintf("\n");
    }
    
    spinlock_unlock(&wq->q_lock);
  }
}
#endif /* DEBUG_WAITQUEUE */
