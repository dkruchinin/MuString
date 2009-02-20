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
#include <ds/pqueue.h>
#include <eza/task.h>
#include <eza/waitqueue.h>
#include <eza/spinlock.h>
#include <eza/scheduler.h>
#include <eza/arch/types.h>
#include <eza/signal.h>
#include <mlibc/stddef.h>
#include <mlibc/types.h>

int waitqueue_insert_core(wqueue_t *wq, wqueue_task_t *wq_task, wqueue_insop_t iop)
{
  int ret = 0;
  
  pqueue_insert_core(&wq->pqueue, &wq_task->pq_node, wq_task->task->static_priority);
  wq->num_waiters++;
  wq_task->wq = wq;

  if( wq_task->wq_stat ) {
    atomic_inc(wq_task->wq_stat);
  }

  if (iop != WQ_INSERT_SIMPLE) {
    ret = sched_change_task_state(wq_task->task, (iop == WQ_INSERT_SLEEP_INR) ?
                                  TASK_STATE_SLEEPING : TASK_STATE_SUSPENDED);
    if (unlikely(ret == -EAGAIN)) {
      ret = 0;
      goto remove_task;
    }
    
    /* Check for pending actions. */
    if (likely(!task_was_interrupted(wq_task->task)))
      goto out;

    ret = -EINTR;
    remove_task:
    wq->num_waiters--;
    wq_task->wq = NULL;
    pqueue_delete_core(&wq->pqueue, &wq_task->pq_node);
  }
  
  out:
  return ret;
}

int waitqueue_insert(wqueue_t *wq, wqueue_task_t *wq_task, wqueue_insop_t iop)
{
  int ret = 0;

  __wqueue_lock(wq);
  ret = waitqueue_insert_core(wq, wq_task, iop);
  __wqueue_unlock(wq);
  return ret;
}

int waitqueue_pop(wqueue_t *wq, task_t **task)
{
  int ret = 0;
  pqueue_node_t *pqn;
  
  __wqueue_lock(wq);
  if (waitqueue_is_empty(wq)) {
    ret = -ENOENT;
    goto out;
  }
  
  pqn = pqueue_pick_min_core(&wq->pqueue);
  ASSERT(pqn != NULL);
  if (task)
    *task = (container_of(pqn, wqueue_task_t, pq_node))->task;

  waitqueue_delete_core(container_of(pqn, wqueue_task_t, pq_node), WQ_DELETE_WAKEUP);
  out:
  __wqueue_unlock(wq);
  return ret;
}

int waitqueue_delete(wqueue_task_t *wq_task, wqueue_delop_t dop)
{
  wqueue_t *wq = wq_task->wq;

  if (!wq_task->wq)
    return 0;
  else {
    int ret;
    
    __wqueue_lock(wq);
    ret = waitqueue_delete_core(wq_task, dop);
    __wqueue_unlock(wq);

    return ret;
  }
}

int waitqueue_delete_core(wqueue_task_t *wq_task, wqueue_delop_t dop)
{
  int ret = 0;
  wqueue_t *wq = wq_task->wq;

  if(!wq_task->wq)
    return 0;
  if (waitqueue_is_empty(wq))
    panic("__waitqueue_delete: Hey! Someone is trying to remove task from an empty wait queue. Liar!\n");

  pqueue_delete_core(&wq->pqueue, &wq_task->pq_node);

  if( wq_task->wq_stat ) {
    atomic_dec(wq_task->wq_stat);
  }

  if (dop == WQ_DELETE_WAKEUP)   
    ret = sched_change_task_state(wq_task->task, TASK_STATE_RUNNABLE);

  wq->num_waiters--;
  wq_task->wq = NULL;
  return ret;
}

wqueue_task_t *waitqueue_first_task(wqueue_t *wq)
{
  wqueue_task_t *wqt;
  
  __wqueue_lock(wq);
  wqt = waitqueue_first_task_core(wq);
  __wqueue_unlock(wq);

  return wqt;
}

wqueue_task_t *waitqueue_first_task_core(wqueue_t *wq)
{
  wqueue_task_t *wqt = NULL;

  if (!waitqueue_is_empty(wq)) {
    pqueue_node_t *pqn;
    
    if ((pqn = pqueue_pick_min_core(&wq->pqueue)))
      wqt = container_of(pqn, wqueue_task_t, pq_node);
  }

  return wqt;
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
    pqueue_node_t *pqn;

    __wqueue_lock(wq);
    kprintf("[WQ dump; Waiters: %d]:\n", wq->num_waiters);
    list_for_each_entry(&wq->pqueue.queue, pqn, node) {
      pqueue_node_t *sub_pqn;

      t = container_of(pqn, wqueue_task_t, pq_node);
      kprintf(" [%d]=> %d", t->task->static_priority, t->task->pid);
      list_for_each_entry(&pqn->head, sub_pqn, node)
        kprintf("->%d", (container_of(sub_pqn, wqueue_task_t, pq_node))->task->pid);

      kprintf("\n");
    }
    
    __wqueue_unlock(wq);
  }
}
#endif /* CONFIG_DEBUG_WAITQUEUE */
