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

#include <config.h>
#include <mlibc/kprintf.h>
#include <eza/scheduler.h>
#include <eza/kernel.h>
#include <eza/smp.h>
#include <eza/arch/scheduler.h>
#include <eza/arch/types.h>
#include <eza/arch/bits.h>
#include <eza/task.h>
#include <eza/scheduler.h>
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
#include <eza/arch/profile.h>
#include <eza/sched_default.h>

extern void initialize_idle_tasks(void);

volatile cpu_id_t online_cpus;

/* Known schedulers. */
static list_head_t schedulers;
static spinlock_t scheduler_lock;
static scheduler_t *active_scheduler = NULL;

#define LOCK_SCHEDULER_LIST spinlock_lock(&scheduler_lock)
#define UNLOCK_SCHEDULER_LIST spinlock_unlock(&scheduler_lock)

static spinlock_t migration_locks[CONFIG_NRCPUS];
static list_head_t migration_actions[CONFIG_NRCPUS];

static void initialize_sched_internals(void)
{
  list_init_head(&schedulers);
  spinlock_initialize(&scheduler_lock);
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

  for(i=0;i<CONFIG_NRCPUS;i++) {
    spinlock_initialize(&migration_locks[i]);
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

int sched_change_task_state_mask(task_t *task,ulong_t state,
                                      ulong_t mask)
{
  /* TODO: [mt] implement security check on task state change. */
  if(active_scheduler == NULL) {
    return -ENOTTY;
  }
  return active_scheduler->change_task_state(task,state,mask);
}

int sched_change_task_state_deferred_mask(task_t *task,ulong_t state,
                                               deferred_sched_handler_t handler,void *data,
                                               ulong_t mask)
{
  if(active_scheduler != NULL &&
     active_scheduler->change_task_state_deferred != NULL) {
    return active_scheduler->change_task_state_deferred(task,state,handler,data,mask);
  }
  return -ENOTTY;
}

int sched_add_task(task_t *task)
{
  if(active_scheduler != NULL) {
    return active_scheduler->add_task(task);
  }
  return -ENOTTY;
}

int sched_setup_idle_task(task_t *task)
{
  if(active_scheduler != NULL) {
    return active_scheduler->setup_idle_task(task);
  }
  return -ENOTTY;
}

int sched_add_cpu(cpu_id_t cpu)
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

int sched_del_task(task_t *task)
{
  if( active_scheduler != NULL ) {
    return active_scheduler->del_task(task);
  }
  return -ENOTTY;
}

int sched_move_task_to_cpu(task_t *task,cpu_id_t cpu)
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

int sys_yield(void)
{
  schedule();
  return 0;
}


long do_scheduler_control(task_t *task, ulong_t cmd, ulong_t arg)
{
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
      if( !trusted_task(current_task()) ) {
        return -EPERM;
      }

      if( !arg || (arg & ~ONLINE_CPUS_MASK) ) {
        return -EINVAL;
      }

      if( task->cpu_affinity_mask != arg ) {
        if( !(task->cpu_affinity_mask & (1 << cpu_id())) ) {
        }
      }

      return 0;
    case SYS_SCHED_CTL_GET_CPU:
      return task->cpu;
    case SYS_SCHED_CTL_SET_CPU:
      if( !trusted_task(current_task()) ) {
        return -EPERM;
      }
      if( (arg >= CONFIG_NRCPUS) || !cpu_affinity_ok(task,arg) ) {
        return -EINVAL;
      }
      return sched_move_task_to_cpu(task,arg);
    default:
      return task->scheduler->scheduler_control(task,cmd,arg);
  }
}

long sys_scheduler_control(pid_t pid, ulong_t cmd, ulong_t arg)
{
  task_t *target;
  long r;

  if(cmd > SCHEDULER_MAX_COMMON_IOCTL) {
    return  -EINVAL;
  }  

  target = pid_to_task(pid);  
  if( target == NULL ) {
    return -ESRCH;
  }
  
  /* if TF_USPC_BLOCKED flag is set, task static priority can not be changed by user */
  if ((cmd == SYS_SCHED_CTL_SET_PRIORITY) && (target->flags & TF_USPC_BLOCKED))
      return -EAGAIN;

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

int sleep(ulong_t ticks)
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
    sched_change_task_state_deferred(current_task(),TASK_STATE_SLEEPING,
                                     __sleep_timer_lazy_routine,&timer);
    delete_timer(&timer);
  }
  return 0;
}

#ifdef CONFIG_SMP
#include <eza/arch/apic.h>

int schedule_task_migration(migration_action_t *a,cpu_id_t cpu)
{
  if( cpu < CONFIG_NRCPUS ) {
    spinlock_lock(&migration_locks[cpu]);
    list_add2tail(&migration_actions[cpu], &a->l);
    spinlock_unlock(&migration_locks[cpu]);
    activate_task(gc_threads[cpu][MIGRATION_THREAD_IDX]);
    return 0;
  } else {
    return -EINVAL;
  }
}

void do_smp_scheduler_interrupt_handler(void)
{
}

static int __move_task_to_this_cpu(task_t *t)
{
  eza_sched_cpudata_t *src_cpu,*dst_cpu;
  eza_sched_taskdata_t *tdata = EZA_TASK_SCHED_DATA(t);
  int r;
  ulong_t is,is2,my_cpu=cpu_id();

lock_cpus_data:
  src_cpu=NULL;
  /* Always lock from from lower CPU numbers to higher. */
  if( my_cpu <= t->cpu ) {
    dst_cpu=sched_cpu_data[my_cpu];
    while(true) {
      interrupts_save_and_disable(is2);
      if( try_to_lock_sched_data(dst_cpu) ) {
        break;
      }
      interrupts_restore(is2);
    }
  } else {
    dst_cpu=NULL;
  }

  if( !(t->flags & __TF_UNDER_MIGRATION_BIT) || tdata->array != NULL ) {
    r=-EBUSY;
    goto unlock;
  }

  if( t->cpu == my_cpu ) {
    r=0;
    goto unlock;
  }

  if( dst_cpu != NULL ) {
    /* Destination CPU is already ours. */
    src_cpu=get_task_sched_data_locked(t,&is,false);
    if( !src_cpu ) {
      /* No luck - do locking one more time. */
      __UNLOCK_CPU_SCHED_DATA(dst_cpu);
      interrupts_enable();
      goto lock_cpus_data;
    }
    /* Ok, go ahead. */
    goto transfer_task;
  } else {
    /* No locks were locked, so lock first task's CPU, than our CPU. */
    src_cpu=get_task_sched_data_locked(t,&is,true);
    dst_cpu=sched_cpu_data[my_cpu];    

    if( !try_to_lock_sched_data(dst_cpu) ) {
      /* Someone else holds the lock, so repeat the procedure. */
      __UNLOCK_CPU_SCHED_DATA(src_cpu);
      interrupts_enable();
      goto lock_cpus_data;
    }
  }

transfer_task:
  /* All CPUs are seem to be locked. So migrate the task. */
  if(t->state != TASK_STATE_SUSPENDED || tdata->array != NULL ) {
    r=-EBUSY;
    goto unlock;
  }
  src_cpu->stats->sleeping_tasks--;
  dst_cpu->stats->sleeping_tasks++;
  t->cpu=my_cpu;
  r=0;

unlock:
  if( src_cpu ) {
    __UNLOCK_CPU_SCHED_DATA(src_cpu);
  }

  if( dst_cpu ) {
    __UNLOCK_CPU_SCHED_DATA(dst_cpu);
  }

  interrupts_enable();
  cond_reschedule();

  /* Mark task as ready for another migrations. */
  atomic_test_and_reset_bit(&t->flags,__TF_UNDER_MIGRATION_BIT);
  if( !r ) {
    activate_task(t);
  }

  return r;
}

void migration_thread(void *data)
{
  if( do_scheduler_control(current_task(),SYS_SCHED_CTL_SET_PRIORITY,
                           EZA_SCHED_NONRT_PRIO_BASE) ) {
    panic( "CPU #%d: migration_thread() can't set its default priority !\n" );
  }

  while(true) {
    list_head_t private, *mytasks;
    cpu_id_t cpu=cpu_id();
    list_node_t *n, *ns;

    mytasks=&migration_actions[cpu];
    list_init_head(&private);
    spinlock_lock(&migration_locks[cpu]);

    if( !list_is_empty(mytasks) ) {
      list_move2head(&private,mytasks);
      spinlock_unlock(&migration_locks[cpu]);

      list_for_each_safe(&private,n,ns) {
        migration_action_t *action=container_of(n,migration_action_t,l);

        list_del(n);
        action->status=__move_task_to_this_cpu(action->task);
        event_raise(&action->e);
      }
    } else {
      spinlock_unlock(&migration_locks[cpu]);
    }
    sched_change_task_state(current_task(),TASK_STATE_SLEEPING);
  }
}

#endif
