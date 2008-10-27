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
#include <eza/scheduler.h>
#include <eza/swks.h>
#include <eza/kstack.h>
#include <mlibc/index_array.h>
#include <eza/task.h>
#include <eza/errno.h>
#include <ds/list.h>
#include <mlibc/stddef.h>
#include <mlibc/string.h>
#include <eza/process.h>
#include <eza/security.h>

extern void initialize_idle_tasks(void);

volatile cpu_id_t online_cpus;

/* Known schedulers. */
static list_head_t schedulers;
static spinlock_t scheduler_lock;
static scheduler_t *active_scheduler = NULL;

#define LOCK_SCHEDULER_LIST spinlock_lock(&scheduler_lock)
#define UNLOCK_SCHEDULER_LIST spinlock_unlock(&scheduler_lock)


static void initialize_sched_internals(void)
{
  list_init_head(&schedulers);
  spinlock_initialize(&scheduler_lock, "Scheduler lock");
}

void initialize_scheduler(void)
{
  initialize_sched_internals();
  initialize_kernel_stack_allocator();
  initialize_task_subsystem();

  /* Now initialize scheduler. */
  list_init_head(&schedulers);
  if( !sched_register_scheduler(get_default_scheduler())) {
    panic( "initialize_scheduler(): Can't register default scheduler !" );  
  }

  initialize_idle_tasks();
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
  if( sched == NULL || sched->cpus_supported == NULL || sched->add_cpu == NULL
     || sched->scheduler_tick == NULL || sched->add_task == NULL
     || sched->schedule == NULL || sched->id == NULL || sched->change_task_state == NULL
     || sched->setup_idle_task == NULL || sched->reset == NULL ) {
    return false;
  }

  sched->reset();

  LOCK_SCHEDULER_LIST;
  
  list_init_node(&sched->l);
  list_add2tail(&schedulers,&sched->l); 
  
  if(active_scheduler == NULL) {
    active_scheduler = sched;
  }

  UNLOCK_SCHEDULER_LIST;

  sched->reset();
  kprintf( "Registering scheduler: '%s' (CPUs supported: %d)\n",
           sched->id, sched->cpus_supported() );

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

status_t sched_change_task_state(task_t *task,task_state_t state)
{
  /* TODO: [mt] implement security check on task state change. */
  if(active_scheduler == NULL) {
    return -ENOTTY;
  }
  return active_scheduler->change_task_state(task,state);
}

status_t sched_change_task_state_lazy(task_t *task,task_state_t state,
                                      lazy_sched_handler_t handler,void *data)
{
  if(active_scheduler != NULL &&
     active_scheduler->change_task_state_lazy != NULL) {
    return active_scheduler->change_task_state_lazy(task,state,handler,data);
  }
  return -ENOTTY;
}

status_t sched_add_task(task_t *task)
{
  if(active_scheduler != NULL) {
    return active_scheduler->add_task(task);
  }
  return -ENOTTY;
}

status_t sched_setup_idle_task(task_t *task)
{
  if(active_scheduler != NULL) {
    return active_scheduler->setup_idle_task(task);
  }
  return -ENOTTY;
}

status_t sched_add_cpu(cpu_id_t cpu)
{
  if( active_scheduler != NULL ) {
    return active_scheduler->add_cpu(cpu);
  }
  return -ENOTTY;
}

void update_idle_tick_statistics(scheduler_cpu_stats_t *stats)
{
  stats->idle_ticks++;
}

void schedule(void)
{
  if(active_scheduler != NULL) {
    active_scheduler->schedule();
  }
}

void sched_timer_tick(void)
{
  if(active_scheduler != NULL) {
    active_scheduler->scheduler_tick();
  }
}

status_t sys_yield(void)
{
  schedule();
  return 0;
}


status_t do_scheduler_control(task_t *task, ulong_t cmd, ulong_t arg)
{
  return task->scheduler->scheduler_control(task,cmd,arg);
}

status_t sys_scheduler_control(pid_t pid, ulong_t cmd, ulong_t arg)
{
  task_t *target = pid_to_task(pid);
  status_t r;

  if( target == NULL ) {
    return -ESRCH;
  }

  if( target->scheduler == NULL ) {
    r = -ENOTTY;
    goto out_release;
  }

  if( !security_ops->check_scheduler_control(target,cmd,arg) ) {
    r = -EACCES;
    goto out_release;
  }

  r = do_scheduler_control(target,cmd,arg);
out_release:
  release_task_struct(target);
  return r;
}

#ifdef CONFIG_SMP
/* SMP-specific stuff. */
void do_smp_scheduler_interrupt_handler(void)
{
}

#endif
