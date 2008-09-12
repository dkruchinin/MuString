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
 * eza/generic_api/scheduler.c: contains implementation of the generic
 *                              scheduler layer.
 *
 */

#include <mlibc/kprintf.h>
#include <eza/scheduler.h>
#include <eza/kernel.h>
#include <eza/swks.h>
#include <eza/smp.h>
#include <eza/arch/scheduler.h>
#include <eza/arch/types.h>
#include <eza/arch/bits.h>
#include <eza/task.h>
#include <mm/pt.h>
#include <eza/scheduler.h>
#include <eza/swks.h>
#include <eza/kstack.h>
#include <mlibc/index_array.h>
#include <eza/task.h>
#include <eza/errno.h>
#include <ds/list.h>
#include <mlibc/stddef.h>
#include <mlibc/string.h>

extern void initialize_idle_tasks(void);

cpu_id_t online_cpus;

/* Known schedulers. */
static list_head_t schedulers;
static spinlock_t scheduler_lock;

#define LOCK_SCHEDULER_LIST spinlock_lock(&scheduler_lock)
#define UNLOCK_SCHEDULER_LIST spinlock_unlock(&scheduler_lock)


static void initialize_sched_internals(void)
{
  list_init_head(&schedulers);
  spinlock_initialize(&schedulers_lock, "Scheduler lock");
}

void initialize_scheduler(void)
{
  initialize_sched_internals();
  initialize_kernel_stack_allocator();
  initialize_task_subsystem();
  initialize_idle_tasks();
}

status_t activate_task(task_t *task)
{
  if( task->state == TASK_STATE_JUST_BORN ||
    task->state == TASK_STATE_STOPPED ) {

    LOCK_TASK_STRUCT(task);
    task->state = TASK_STATE_RUNNABLE;
    UNLOCK_TASK_STRUCT(task);
    return 0;
  }
  return -EINVAL;
}

void reschedule_task(task_t *task) {
}

status_t deactivate_task(task_t *task, task_state_t state)
{
  if( task->state == TASK_STATE_RUNNABLE ||
    task->state == TASK_STATE_RUNNING ) {
    task_state_t prev_state = task->state;

    task->state = state;
    if( prev_state == TASK_STATE_RUNNABLE ) {
      /* Move targe task to the list of stopped tasks. */
    }
    UNLOCK_TASK_STRUCT(task);

    /* In case target task is running, we must reschedule it. */
    if( prev_state == TASK_STATE_RUNNING ) {
      reschedule_task(task);
    }

    return 0;
  }
  return -EINVAL;
}


status_t sched_do_change_task_state(task_t *task,task_state_t state)
{
  /* TODO: [mt] implement security check on task state change. */
  switch(state) {
    case TASK_STATE_RUNNABLE:
      return activate_task(task);
    case TASK_STATE_STOPPED:
      return deactivate_task(task,TASK_STATE_STOPPED);
    default:
      break;
  }

  return -EINVAL;
}

scheduler_t *sched_get_scheduler(const char *name)
{
  scheduler_t *sched = NULL;
  list_node_t *l;

  LOCK_SCHEDULER_LIST;
  list_for_each(&schedulers,l) {
    scheduler_t *s = container_of(l,scheduler_t,l);
    if( !strcmp(s->id,name) ) {
      sched = s;
      break;
    }
  }
  UNLOCK_SCHEDULER_LIST;

  return sched;
}

bool sched_register_scheduler(scheduler_t *sched)
{
  LOCK_SCHEDULER_LIST;
  
  list_init_node(&sched->l);
  list_add2tail(&schedulers,&sched->l); 
  
  UNLOCK_SCHEDULER_LIST;

  return true;
}

bool sched_unregister_scheduler(scheduler_t *sched)
{
  bool r = false;
  list_node_t *l;

  LOCK_SCHEDULER_LIST;
  list_for_each(&schedulers,l) {
    if( l == &sched->l ) {
      r = true;
      list_del(&sched->l);
      break;
    }
  }
  UNLOCK_SCHEDULER_LIST;
  return r;
}

void schedule(void)
{
}

void sched_timer_tick(void)
{
}

