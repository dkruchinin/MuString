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
 * (c) Copyright 2006,2007,2008 MString Core Team <http://mstring.jarios.org>
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

volatile cpu_id_t online_cpus = 0;

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

void schedule(void)
{
  if(active_scheduler != NULL) {
    active_scheduler->schedule();
  }
}

void sched_timer_tick(void)
{
  if(active_scheduler != NULL) {
    active_scheduler->scheduler_tick(__SCHED_TICK_NORMAL);
  }
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


#ifdef CONFIG_SMP
#include <eza/arch/apic.h>

static void __self_move_trampoline(gc_action_t *action)
{
  migration_action_t *a=(migration_action_t *)(action->data);
  cpu_id_t cpu=a->cpu;

  suspend_task(a->task);
  spinlock_lock(&migration_locks[cpu]);
  list_add2tail(&migration_actions[cpu], &a->l);
  spinlock_unlock(&migration_locks[cpu]);
  activate_task(gc_threads[cpu][MIGRATION_THREAD_IDX]);
}

static void __self_move_gc_actor(gc_action_t *action)
{
  migration_action_t a;

  INIT_MIGRATION_ACTION(&a,current_task(),(long)action->data);

  action->data=&a;
  action->action=__self_move_trampoline;

  gc_schedule_action(action);
  event_yield(&a.e);
}

void do_smp_scheduler_interrupt_handler(void)
{
}

/* NOTE: Task must have flag __TF_UNDER_MIGRATION_BIT set !
 * If target task is not running, it is moved to target CPU,
 * flag __TF_UNDER_MIGRATION_BIT is reset and zero is returned.
 * Otherwise, nothing is changed and -EBUSY is returned.
 */
static long __move_task_to_cpu(task_t *task,cpu_id_t dest_cpu,bool cyclic)
{
  eza_sched_cpudata_t *src_data,*dst_data=sched_cpu_data[dest_cpu];
  long is,r=-EBUSY;

lock_src_cpu:
  src_data=get_task_sched_data_locked(task,&is,false);
  if( !src_data ) {
    if( cyclic ) {
      goto lock_src_cpu;
    } else {
      goto out;
    }
  }

  if( task->cpu == dest_cpu ) { /* Already on target CPU ? */
    r=0;
    goto unlock_src_data;
  }

  /* At this point local interrupts are already disabled. */
  if( !try_to_lock_sched_data(dst_data) ) {
    __UNLOCK_CPU_SCHED_DATA(src_data);
    interrupts_restore(is);
    cond_reschedule();

    if( cyclic ) {
      goto lock_src_cpu;
    } else {
      goto out;
    }
  }

  /* OK, all CPUs are locked, so try to move the task. */
  if( !(task->state & (TASK_STATE_RUNNING | TASK_STATE_RUNNABLE)) ) {
    src_data->stats->sleeping_tasks--;
    dst_data->stats->sleeping_tasks++;
    task->cpu=dest_cpu;
    r=0;
  }

  __UNLOCK_CPU_SCHED_DATA(dst_data);
unlock_src_data:
  if( !r ) {
    atomic_test_and_clear_bit(&task->flags,__TF_UNDER_MIGRATION_BIT);
  }

  __UNLOCK_CPU_SCHED_DATA(src_data);
  interrupts_restore(is);
  cond_reschedule();
out:
  return r;
}

int sched_move_task_to_cpu(task_t *task,cpu_id_t cpu)
{
  gc_action_t *a;

  if(cpu >= EZA_SCHED_CPUS || !is_cpu_online(cpu)) {
    return -EINVAL;
  }

  if( task->cpu == cpu ) {
    return 0;
  }

  /* Prevent target task from double migration. */
  if( atomic_test_and_set_bit(&task->flags,__TF_UNDER_MIGRATION_BIT) ) {
    return -EBUSY;
  }

  if( __move_task_to_cpu(task,cpu,false) ) {
    /* Task can't be moved right now, so schedule a deferred migration.
     */
      a=gc_allocate_action(__self_move_gc_actor,(void *)(long)cpu);
    schedule_user_deferred_action(task,a,false);
  }

  return 0;
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
        kprintf("*** Moving TID 0x%X to cpu %d.\n",
                        action->task->tid,cpu_id());
        if( __move_task_to_cpu(action->task,cpu_id(),true) ) {
          panic("[CPU %d] migration_thread(): Can't move TID 0x%X to my CPU !\n",
                cpu_id(),action->task->tid);
        }
        kprintf("*** TID 0x%X was moved to cpu %d.\n",
                action->task->tid,cpu_id());
        event_raise(&action->e);
      }
    } else {
      spinlock_unlock(&migration_locks[cpu]);
    }
    sched_change_task_state(current_task(),TASK_STATE_SLEEPING);
  }
}

#else

int sched_move_task_to_cpu(task_t *task,cpu_id_t cpu) {
  return 0;
}

#endif
