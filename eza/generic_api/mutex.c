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
 * eza/generic_api/mutex.c: binary semaphore(mutex) with
 *  priority inheritance opportunity to prevent priority inversion.
 */


#include <ds/waitqueue.h>
#include <eza/scheduler.h>
#include <eza/task.h>
#include <eza/spinlock.h>
#include <eza/mutex.h>
#include <eza/arch/types.h>

void mutex_initialize(mutex_t *mutex)
{
  spinlock_initialize(&mutex->lock);
  waitqueue_initialize(&mutex->wq);
  mutex->executer.task = NULL;
  mutex->executer.priority = TASK_PRIO_INVAL;
  mutex->max_prio = TASK_PRIO_INVAL;
}

void mutex_lock(mutex_t *mutex)
{
  if (!mutex_is_locked(mutex)) {
    spinlock_lock(&mutex->lock);
    mutex->executer.task = current_task();
    mutex->max_prio = do_scheduler_control(current_task(), SYS_SCHED_CTL_GET_PRIORITY, 0);
    mutex->executer.priority = mutex->max_prio;
    ASSERT(mutex->max_prio >= 0);
    spinlock_unlock(&mutex->lock);    
  }
  else {
    wait_queue_task_t cur;

    spinlock_lock(&mutex->lock);
    waitqueue_prepare_task(&cur, current_task());
    if (cur.priority < mutex->max_prio) {
      status_t ret;        
      
      ret = do_scheduler_control(mutex->executer.task, SYS_SCHED_CTL_SET_PRIORITY, cur.priority);
      if (ret)
        panic("mutex_lock: do_scheduler_control returned an error: %d", ret);

      mutex->max_prio = cur.priority;
    }

    waitqueue_dump(&mutex->wq);
    spinlock_unlock(&mutex->lock);
    waitqueue_push(&mutex->wq, &cur);
  }
}

void mutex_unlock(mutex_t *mutex)
{
  if (!mutex_is_locked(mutex))
    return;
  else {
    uint32_t prio;
    wait_queue_task_t *wq_task;

    spinlock_lock(&mutex->lock);    
    ASSERT(mutex->executer.task == current_task());
    /* FIXME: if this code located here, panic occurs in shceduler's __shuffle_task... 
    prio = do_scheduler_control(current_task(), SYS_SCHED_CTL_GET_PRIORITY, 0);
    if (prio != mutex->executer.priority)
    do_scheduler_control(current_task(), SYS_SCHED_CTL_SET_PRIORITY, mutex->executer.priority);*/

    prio = mutex->executer.priority;
    wq_task = waitqueue_first_task(&mutex->wq);
    if (wq_task) {
      mutex->executer.task = wq_task->task;
      mutex->executer.priority = wq_task->priority;      
      if (mutex->max_prio < wq_task->priority)
        mutex->max_prio = wq_task->priority;

      waitqueue_delete(&mutex->wq, wq_task, WQ_DELETE_WAKEUP);
      waitqueue_dump(&mutex->wq);
      goto out;
    }

    mutex->executer.task = NULL;
    mutex->executer.priority = TASK_PRIO_INVAL;
    mutex->max_prio = TASK_PRIO_INVAL;
    
    out:
    if (prio != do_scheduler_control(current_task(), SYS_SCHED_CTL_GET_PRIORITY, 0))
      do_scheduler_control(current_task(), SYS_SCHED_CTL_SET_PRIORITY, prio);
    
    spinlock_unlock(&mutex->lock);
  }
}

bool mutex_trylock(mutex_t *mutex)
{
  bool stat;

  if (!(stat = mutex_is_locked(mutex)))
    mutex_lock(mutex);

  return stat;
}

