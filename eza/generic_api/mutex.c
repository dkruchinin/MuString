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
  waitqueue_initialize(&mutex->wq, WQ_PRIO);
  mutex->executer.task = NULL;
  mutex->executer.priority = TASK_PRIO_INVAL;
  mutex->max_prio = TASK_PRIO_INVAL;
}

void mutex_lock(mutex_t *mutex)
{
  spinlock_lock(&mutex->lock);
  if (!mutex_is_locked(mutex)) {
    kprintf("locker: %d\n", current_task()->pid);
    mutex->executer.task = current_task();
    mutex->max_prio = current_task()->static_priority;
    mutex->executer.priority = mutex->max_prio;
    spinlock_unlock(&mutex->lock);
  }
  else {
    wqueue_task_t cur;
    
    if (mutex->executer.task == current_task()) {
      kprintf(KO_WARNING, "Task %p with pid %d locks already locked by itself mutex %p!\n",
              current_task(), current_task()->pid, mutex);
      spinlock_unlock(&mutex->lock);
      return;
    }

    waitqueue_prepare_task(&cur, current_task());
    if (cur.task->static_priority < mutex->max_prio) {
      status_t ret;        

      kprintf("change prio to(%d) %d\n", mutex->executer.task->pid, cur.task->static_priority);
      ret = do_scheduler_control(mutex->executer.task, SYS_SCHED_CTL_SET_PRIORITY, cur.task->static_priority);
      if (ret)
        panic("mutex_lock: do_scheduler_control returned an error: %d", ret);

      mutex->max_prio = cur.task->static_priority;
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
    wqueue_task_t *wq_task;

    spinlock_lock(&mutex->lock);
    prio = mutex->executer.priority;
    if (prio != current_task()->static_priority)
      do_scheduler_control(current_task(), SYS_SCHED_CTL_SET_PRIORITY, mutex->executer.priority);

    prio = mutex->executer.priority;
    wq_task = waitqueue_first_task(&mutex->wq);
    if (wq_task) {
      mutex->executer.task = wq_task->task;
      if (mutex->max_prio < wq_task->task->static_priority)
        mutex->max_prio = wq_task->task->static_priority;

      waitqueue_delete(wq_task, WQ_DELETE_WAKEUP);
      waitqueue_dump(&mutex->wq);
      goto out;
    }

    mutex->executer.task = NULL;
    mutex->executer.priority = TASK_PRIO_INVAL;
    mutex->max_prio = TASK_PRIO_INVAL;
    
    out:
    spinlock_unlock(&mutex->lock);
  }
}

bool mutex_trylock(mutex_t *mutex)
{
  bool stat;

  if ((stat = !mutex_is_locked(mutex)))
    mutex_lock(mutex);
  
  return stat;
}

