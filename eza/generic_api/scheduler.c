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
#include <eza/process.h>
#include <eza/security.h>
#include <eza/timer.h>
#include <eza/time.h>
#include <kernel/syscalls.h>
#include <eza/kconsole.h>
#include <eza/gc.h>
#include <eza/time.h>

extern void initialize_idle_tasks(void);

volatile cpu_id_t online_cpus;

/* Known schedulers. */
static list_head_t schedulers;
static spinlock_t scheduler_lock;
static scheduler_t *active_scheduler = NULL;

#define LOCK_SCHEDULER_LIST spinlock_lock(&scheduler_lock)
#define UNLOCK_SCHEDULER_LIST spinlock_unlock(&scheduler_lock)

static list_head_t migration_lists[NR_CPUS];
static spinlock_t migration_locks[NR_CPUS];
static list_head_t migration_actions[NR_CPUS];

static void initialize_sched_internals(void)
{
  list_init_head(&schedulers);
  spinlock_initialize(&scheduler_lock, "Scheduler lock");
}

void initialize_scheduler(void)
{
  int i;

  initialize_sched_internals();
  initialize_kernel_stack_allocator();
  initialize_task_subsystem();

  /* Now initialize scheduler. */
  list_init_head(&schedulers);
  if( !sched_register_scheduler(get_default_scheduler())) {
    panic( "initialize_scheduler(): Can't register default scheduler !" );  
  }

  initialize_idle_tasks();

  for(i=0;i<NR_CPUS;i++) {
    spinlock_initialize(&migration_locks[i]);
    list_init_head(&migration_lists[i]);
    list_init_head(&migration_actions[i]);
  }
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

status_t sched_del_task(task_t *task)
{
  if( active_scheduler != NULL ) {
    return active_scheduler->del_task(task);
  }
  return -ENOTTY;
}

status_t sched_move_task_to_cpu(task_t *task,cpu_id_t cpu)
{
  if( active_scheduler != NULL && active_scheduler->move_task_to_cpu ) {
      return active_scheduler->move_task_to_cpu(task,cpu);
  }
  return -ENOTTY;
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
  status_t r;

  switch( cmd ) {
    case SYS_SCHED_CTL_GET_AFFINITY_MASK:
      return task->cpu_affinity_mask;
    case SYS_SCHED_CTL_GET_PRIORITY:
      return task->static_priority;
    case SYS_SCHED_CTL_GET_STATE:
      return task->state;

    case SYS_SCHED_CTL_MONOPOLIZE_CPU:
    case SYS_SCHED_CTL_DEMONOPOLIZE_CPU:
      return -EINVAL;

    case SYS_SCHED_CTL_SET_AFFINITY_MASK:
      if( !arg || (arg & ~ONLINE_CPUS_MASK) ) {
        return -EINVAL;
      }

      LOCK_TASK_STRUCT(task);
      if( task->cpu_affinity_mask != arg ) {
        if( !(task->cpu_affinity_mask & (1 << cpu_id())) ) {
          /* Schedule immediate migration. */
          
        }
      }
      UNLOCK_TASK_STRUCT(task);

      return 0;
    default:
      return task->scheduler->scheduler_control(task,cmd,arg);
  }
}

status_t sys_scheduler_control(pid_t pid, ulong_t cmd, ulong_t arg)
{
  task_t *target;
  status_t r;

  if(cmd > SCHEDULER_MAX_COMMON_IOCTL) {
    return  -EINVAL;
  }

  target = pid_to_task(pid);
  if( target == NULL ) {
    return -ESRCH;
  }

  if( target->scheduler == NULL ) {
    r = -ENOTTY;
    goto out_release;
  }

  if( !security_ops->check_scheduler_control(target,cmd,arg) ) {
    r = -EPERM ;
    goto out_release;
  }

  r = do_scheduler_control(target,cmd,arg);
out_release:
  release_task_struct(target);
  return r;
}

extern int sched_verbose1;

static void __sleep_timer_handler(ulong_t data)
{
  sched_verbose1=0;
  sched_change_task_state((task_t *)data,TASK_STATE_RUNNABLE);
}

static bool __sleep_timer_lazy_routine(void *data)
{
  timer_t *t=(timer_t*)data;
  return t->time_x > system_ticks;
}

status_t sleep(ulong_t ticks)
{
  if( ticks ) {
    timer_t timer;

    init_timer(&timer);
    timer.handler=__sleep_timer_handler;
    timer.data=(ulong_t)current_task();
    timer.time_x=system_ticks+ticks;

    if( !add_timer(&timer) ) {
      return -EAGAIN;
    }
    sched_change_task_state_lazy(current_task(),TASK_STATE_SLEEPING,
                                 __sleep_timer_lazy_routine,&timer);

    delete_timer(&timer);
  }
  return 0;
}

#ifdef CONFIG_SMP

static void __remote_task_state_change_action(void *data,ulong_t data_arg)
{
  task_t *task=(task_t*)data;

  /* First, remove the task from the list. */
  interrupts_disable();
  LOCK_TASK_STRUCT(task);
  if( list_node_is_bound( &task->migration_list ) ) {
    list_del(&task->migration_list);
  }
  UNLOCK_TASK_STRUCT(task);
  interrupts_enable();

  sched_change_task_state(task,data_arg);

  release_task_struct(task);
}

status_t schedule_remote_task_state_change(task_t *task,ulong_t state)
{
  long is;
  gc_action_t *action=gc_allocate_action(__remote_task_state_change_action,
                                        task,state);
  bool go=false;
  status_t r;

  if( !action ) {
    return -ENOMEM;
  }

  interrupts_save_and_disable(is);
  LOCK_TASK_STRUCT(task);

  if( task->cpu != cpu_id() ) {
    if( !list_node_is_bound(&task->migration_list) ) {
      ulong_t cpu=task->cpu;

      spinlock_lock(&migration_locks[cpu]);
      list_add2tail(&migration_actions[cpu], &action->l);
      list_add2tail(&action->data_list_head, &task->migration_list);
      spinlock_unlock(&migration_locks[cpu]);
      go=true;
    } else {
      r=-EBUSY;
    }
  } else {
    r=-EAGAIN;
  }

  UNLOCK_TASK_STRUCT(task);
  interrupts_restore(is);

  if( go ) {
    apic_broadcast_ipi_vector(SCHEDULER_IPI_IRQ_VEC);
    r=0;
  } else {
    gc_free_action(action);
  }
  return r;
}

/* NOTE: Task must be locked before calling this function ! */
status_t schedule_migration(task_t *task,cpu_id_t cpu)
{
  if( cpu < NR_CPUS ) {
    if( !list_node_is_bound(&task->migration_list) ) {
      /* Make one extra reference to prevent this task structure from
       * being freed concurrently (via 'sys_exit()', for example.
       */
      grab_task_struct(task);

      spinlock_lock(&migration_locks[cpu]);
      list_add2tail(&migration_lists[cpu], &task->migration_list);
      spinlock_unlock(&migration_locks[cpu]);
      apic_broadcast_ipi_vector(SCHEDULER_IPI_IRQ_VEC);
      return 0;
    } else {
      return -EBUSY;
    }
  }
  return -EINVAL;
}

void do_smp_scheduler_interrupt_handler(void)
{
  if( !gc_threads[cpu_id()][MIGRATION_THREAD_IDX] ) {
    panic( "Thread migration IPI: Can't launch migration thread for CPU #%d!\n",
           cpu_id());
  }
  sched_change_task_state(gc_threads[cpu_id()][MIGRATION_THREAD_IDX],
                          TASK_STATE_RUNNABLE);
}

/* TODO: [mt] Implement thread migration via 'gc_action_t'. */
void migration_thread(void *data)
{
  while(true) {
    list_head_t private, *mytasks;
    cpu_id_t cpu=cpu_id();
    list_node_t *n, *ns;

    list_init_head(&private);
    mytasks=&migration_lists[cpu];
    spinlock_lock(&migration_locks[cpu]);

    /* Quest N1: process all threads that were forced to migrate to this CPU.
     */
    if( !list_is_empty(mytasks) ) {
      list_move2head(&private,mytasks);
      spinlock_unlock(&migration_locks[cpu]);

      list_for_each_safe(&private,n,ns) {
        task_t *task=container_of(n,task_t,migration_list);

        if( task->cpu != cpu ) {
          panic( "Tasks's CPUs differ after migration ! %d:%d\n",
                 cpu,task->cpu);
        }

        list_del(n);
        sched_change_task_state(task,TASK_STATE_RUNNABLE);
      }
    } else {
      spinlock_unlock(&migration_locks[cpu]);
    }

    /* Quest N2: Process asynchronous task-repated requests (like
     * state change, priority change, etc ...
     */
    mytasks=&migration_actions[cpu];
    list_init_head(&private);
    spinlock_lock(&migration_locks[cpu]);

    if( !list_is_empty(mytasks) ) {
      list_move2head(&private,mytasks);
      spinlock_unlock(&migration_locks[cpu]);

      list_for_each_safe(&private,n,ns) {
        gc_action_t *action=container_of(n,gc_action_t,l);

        list_del(n);
        action->action(action->data,action->data_arg);
        action->dtor(action);
      }
    } else {
      spinlock_unlock(&migration_locks[cpu]);
    }
    sched_change_task_state(current_task(),TASK_STATE_SLEEPING);
  }
}

#endif
