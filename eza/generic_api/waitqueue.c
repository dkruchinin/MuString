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

/* FIXME DK: should I move this stuff to the waitqueue.h?
 * default wait queue sleep/wakeup policy manipulates with only one flag
 * If it is set in the wait queue task structure, then event is occured and
 * task may not be switched to the sleeping state and it should be removed
 * from the queue without any state modifications instead.
 *
 * Dereferred changing task state(that depends on wait queue task flags) guaranties
 * that there won't be any deadlocks even when wait queue is used as a generic part
 * of event-based primitives(for example ipc ports).
 *
 * If user needs some other policy as for example making solution about changing task state
 * depending on several different events, he/she is able to override sleep_if_needful and
 * pre_wakeup functions of a given wait queue. Note, that second function may be disabled at all.
 */
#define WQ_EVENT_OCCURED 0x01

static void __insert_task(wqueue_t *wq, wqueue_task_t *wq_task)
{
  list_node_t *next = NULL;

  next = list_head(&wq->waiters);
  if (likely(!list_is_empty(&wq->waiters))) {
    wqueue_task_t *t;
    
    list_for_each_entry(&wq->waiters, t, node) {
      /* If t is less prioritized then wq_task, then second one will be inserted before the first  */
      if (t->task->static_priority > wq_task->task->static_priority) {
        next = &t->node;
        break;
      }
      /* if we found  */
      if (t->task->static_priority == wq_task->task->static_priority) {
        next = list_head(&t->head);
        break;
      }
    }
  }

  list_add(next, &wq_task->node);
  wq_task->q = wq;
  wq->num_waiters++;
}

static void __delete_task(wqueue_task_t *wq_task)
{    
  if (!list_is_empty(&wq_task->head)) {
    wqueue_task_t *t = list_entry(list_node_first(&wq_task->head),
                                      wqueue_task_t, node);
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

bool __wq_sleep_if_needful(void *data)
{
  return !(((wqueue_task_t *)data)->eflags & WQ_EVENT_OCCURED);
}

void __wq_pre_wakeup(void *data)
{
  ((wqueue_task_t *)data)->eflags &= ~WQ_EVENT_OCCURED;
}

void waitqueue_initialize(wqueue_t *wq)
{  
  list_init_head(&wq->waiters);
  wq->num_waiters = 0;
  spinlock_initialize(&wq->q_lock);
  wq->acts.sleep_if_needful = __wq_sleep_if_needful;
  wq->acts.pre_wakeup = __wq_pre_wakeup;
}

void waitqueue_prepare_task(wqueue_task_t *wq_task, task_t *task)
{
  list_init_head(&wq_task->head);
  wq_task->task = task;
  wq_task->q = NULL;
  spinlock_lock(&task->member_lock);
  wq_task->uspc_blocked = task->flags & TF_USPC_BLOCKED;
  task->flags |= TF_USPC_BLOCKED; /* block priority changing from user-space */
  spinlock_unlock(&task->member_lock);
}

status_t waitqueue_insert(wqueue_t *wq, wqueue_task_t *wq_task, wqueue_insop_t iop)
{
  status_t ret = 0;

  spinlock_lock(&wq->q_lock);
  ASSERT(wq->acts.sleep_if_needful != NULL);
  __insert_task(wq, wq_task);
  spinlock_unlock(&wq->q_lock);
  if (iop == WQ_INSERT_SLEEP) {
      ret = sched_change_task_state_deferred(wq_task->task, TASK_STATE_SLEEPING,
                                             wq->acts.sleep_if_needful, wq_task);
  }
    
  return ret;
}

status_t waitqueue_pop(wqueue_t *wq, task_t **task)
{
  status_t ret = 0;
  wqueue_task_t *wq_t;
  
  spinlock_lock(&wq->q_lock);
  if (waitqueue_is_empty(wq)) {
    ret = -EINVAL;
    goto out;
  }

  wq_t = list_entry(list_node_first(&wq->waiters), wqueue_task_t, node);
  if (task)
    *task = wq_t->task;
  
  ret = __waitqueue_delete(wq_t, WQ_DELETE_WAKEUP);
  out:
  spinlock_unlock(&wq->q_lock);
  waitqueue_dump(wq);
  return ret;
}


/*
 * NOTE: this function doesn't care about locks, so
 * if you really want to use it directly, don't forget to lock wq_task's
 * parent wait queue by yourself.
 */
status_t __waitqueue_delete(wqueue_task_t *wq_task, wqueue_delop_t dop)
{
  status_t ret = 0;
  wqueue_t *wq = wq_task->q;

  if (waitqueue_is_empty(wq))
    panic("__waitqueue_delete: Hey! Someone is trying to remove task from an empty wait queue. Liar!\n");

  __delete_task(wq_task);  
  if (!wq_task->uspc_blocked)
    wq_task->task->flags &= ~TF_USPC_BLOCKED;
  if (wq->acts.pre_wakeup)
    wq->acts.pre_wakeup(wq_task);
  if (dop == WQ_DELETE_WAKEUP)   
    ret = sched_change_task_state(wq_task->task, TASK_STATE_RUNNABLE);
  
  return ret;
}

wqueue_task_t *waitqueue_first_task(wqueue_t *wq)
{
  wqueue_task_t *t = NULL;

  spinlock_lock(&wq->q_lock);
  if (!waitqueue_is_empty(wq))
    t = list_entry(list_node_first(&wq->waiters), wqueue_task_t, node);

  spinlock_unlock(&wq->q_lock);
  return t;
}

#ifdef CONFIG_DEBUG_WAITQUEUE
#include <mlibc/kprintf.h>

void waitqueue_dump(wqueue_t *wq)
{
  if (waitqueue_is_empty(wq)) {
    kprintf("[WQ empty]\n");
    return;
  }
  else {
    wqueue_task_t *t;

    spinlock_lock(&wq->q_lock);    
    kprintf("[WQ dump; Waiters: %d]:\n", wq->num_waiters);
    list_for_each_entry(&wq->waiters, t, node) {
      wqueue_task_t *subt;
      
      kprintf(" [%d]=> %d", t->task->static_priority, t->task->pid);
      list_for_each_entry(&t->head, subt, node)
        kprintf("->%d", subt->task->pid);

      kprintf("\n");
    }
    
    spinlock_unlock(&wq->q_lock);
  }
}
#endif /* CONFIG_DEBUG_WAITQUEUE */
