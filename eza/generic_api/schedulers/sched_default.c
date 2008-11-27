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
 * (C) Copyright 2008 Michael Tsymbalyuk <mtzaurus@gmail.com>
 *
 * eza/generic_api/schedulers/sched_default.c: Implementation of EZA default
 *    scheduler.
 */

#include <eza/arch/types.h>
#include <eza/resource.h>
#include <mm/mmap.h>
#include <mm/pfalloc.h>
#include <eza/kstack.h>
#include <eza/spinlock.h>
#include <eza/arch/current.h>
#include <ds/list.h>
#include <eza/scheduler.h>
#include <eza/errno.h>
#include <eza/sched_default.h>
#include <mlibc/assert.h>
#include <mlibc/string.h>
#include <eza/arch/preempt.h>
#include <eza/swks.h>
#include <eza/arch/interrupt.h>
#include <eza/security.h>
#include <eza/interrupt.h>
#include <eza/arch/profile.h>
#include <eza/arch/preempt.h>
#include <eza/arch/asm.h>
#include <eza/kconsole.h>
#include <eza/gc.h>
#include <eza/arch/apic.h>

/* Our own scheduler. */
static struct __scheduler eza_default_scheduler;

/* Glabal per-CPU array. */
static eza_sched_cpudata_t *sched_cpu_data[EZA_SCHED_CPUS];
static spinlock_t cpu_data_lock;

#define LOCK_CPU_ARRAY()                        \
    spinlock_lock(&cpu_data_lock)

#define UNLOCK_CPU_ARRAY()                      \
    spinlock_unlock(&cpu_data_lock)

#define LOCK_CPU_SCHED_DATA(d)                  \
    spinlock_lock(&d->lock)

#define UNLOCK_CPU_SCHED_DATA(d)                \
    spinlock_unlock(&d->lock)

#define CPU_SCHED_DATA() sched_cpu_data[cpu_id()]

#define EZA_TASK_SCHED_DATA(t) ((eza_sched_taskdata_t *)t->sched_data)

#define PRIO_TO_TIMESLICE(p) (p*3*1)

#define LOCK_EZA_SCHED_DATA(t)                  \
  spinlock_lock(&t->sched_lock)

#define UNLOCK_EZA_SCHED_DATA(t) \
  spinlock_unlock(&t->sched_lock)

#define __ENTER_USER_ATOMIC()                 \
    interrupts_disable()

#define __LEAVE_USER_ATOMIC()                   \
    interrupts_enable();                        \
    cond_reschedule()

static eza_sched_taskdata_t *allocate_task_sched_data(void)
{
  /* TODO: [mt] Allocate memory via slabs !!!  */
  page_frame_t *page = alloc_page(AF_PGEN);
  return (eza_sched_taskdata_t *)pframe_to_virt(page);
}

int sched_verbose=0;

static void free_task_sched_data(eza_sched_taskdata_t *data)
{
  /* TODO: [mt] Free structure via slabs ! */
}

static inline void __dump_prio_array(eza_sched_cpudata_t *sched_data,
                                     int prio, char *s)
{
    list_head_t *lh=&sched_data->active_array->queues[prio];
    list_node_t *ln;

    kprintf( "> [%s] Dumping prio %d:",s,prio );
    list_for_each( lh, ln ) {
        eza_sched_taskdata_t *td = container_of(ln,eza_sched_taskdata_t,
                                                runlist);
        kprintf( " %d ",td->task->pid );
    }
    kprintf( "\n" );
}
 
static eza_sched_cpudata_t *allocate_cpu_sched_data(cpu_id_t cpu) {
  /* TODO: [mt] Allocate memory via slabs !!!  */
  page_frame_t *page = alloc_pages(16, AF_PGEN);
  eza_sched_cpudata_t *cpudata = (eza_sched_cpudata_t *)pframe_to_virt(page);

  if( cpudata != NULL ) {
    initialize_cpu_sched_data(cpudata, cpu);
  }

  return cpudata;
}

static void free_cpu_sched_data(eza_sched_cpudata_t *data)
{
  /* TODO: [mt] Free structure via slabs ! */
}

static void initialize_cpu_sched_data(eza_sched_cpudata_t *cpudata, cpu_id_t cpu)
{
  uint32_t arr,i;

  for( arr=0; arr<EZA_SCHED_NUM_ARRAYS; arr++ ) {
    eza_sched_prio_array_t *array = &cpudata->arrays[arr];
    list_head_t *lh = &array->queues[0];

    memset(&array->bitmap[0], EZA_SCHED_BITMAP_PATTERN, sizeof(eza_sched_type_t)*EZA_SCHED_TOTAL_WIDTH);

    for( i=0; i<EZA_SCHED_TOTAL_PRIOS; i++ ) {
      list_init_head(lh);
      lh++;
    }
  }

  spinlock_initialize(&cpudata->lock);
  cpudata->active_array = &cpudata->arrays[0];

  /* Initialize scheduler statistics for this CPU. */
  cpudata->stats = &swks.cpu_stat[cpu].sched_stats;
  cpudata->cpu_id = cpu;
}

static status_t setup_new_task(task_t *task)
{
  eza_sched_taskdata_t *sdata = allocate_task_sched_data();

  if( sdata == NULL ) {
    return -ENOMEM;
  }

  list_init_node(&sdata->runlist);
  spinlock_initialize(&sdata->sched_lock);

  LOCK_TASK_STRUCT(task);
  task->sched_data = sdata;
  task->scheduler = &eza_default_scheduler;
  sdata->task = task;
  UNLOCK_TASK_STRUCT(task);
  return 0;
}

/* NOTE: This function relies on _current_ state of the task being processed.
 */
static inline void __recalculate_timeslice_and_priority(task_t *task)
{
  eza_sched_taskdata_t *tdata = EZA_TASK_SCHED_DATA(task);
  
  /* Recalculate priority. */
  task->priority = task->static_priority;

  switch(task->state) {
    case TASK_STATE_SLEEPING:
      break;
    default:
      break;
  }
  
  /* Recalculate timeslice. */
  tdata->time_slice = PRIO_TO_TIMESLICE(task->priority);

  /* Are we jailed ? */
  if( tdata->max_timeslice != 0 ) {
    if( tdata->time_slice > tdata->max_timeslice ) {
      tdata->time_slice = tdata->max_timeslice;
    }
  }
}

/* Task must be locked while calling this function !
 */
static inline status_t __activate_local_task(task_t *task, eza_sched_cpudata_t *sched_data)
{
  __recalculate_timeslice_and_priority(task);
  task->state = TASK_STATE_RUNNABLE;
  __add_task_to_array(sched_data->active_array,task);
  sched_data->stats->active_tasks++;

//  kprintf( "++ ACTIVATING LOCAL TASK: %d NEW PRIO: %d, CURRENT: %p, CRRENT PRIO: %d\n",
//           task->pid, EZA_TASK_PRIORITY(task), current_task()->pid, cdata->priority );
  if( task->priority < current_task()->priority ) {
    sched_set_current_need_resched();
  }

  return 0;
}

/* NOTE: Task must be locked while calling this function !
 */
static inline void __deactivate_local_task(task_t *task, eza_sched_cpudata_t *sched_data)
{
  eza_sched_taskdata_t *tdata = EZA_TASK_SCHED_DATA(task);

//  kprintf( "+++++++++++ DEACTIVATED LOCAL TASK: %d, STATE: %d\n",
//           task->pid,task->state );
  /* In case target task is running, we must reschedule it. */
  if( task->state == TASK_STATE_RUNNING ) {
//    kprintf( ">>>> SETTING 'NEED RESCHEDULE'\n" );
    sched_set_current_need_resched();
  }

  task->state = TASK_STATE_STOPPED;

  __remove_task_from_array(tdata->array,task);

  sched_data->stats->active_tasks--;
  sched_data->stats->sleeping_tasks++;
  //kprintf( "+++++++++++ DEACTIVATED LOCAL TASK: %d\n", task->pid );
//  __dump_prio_array(sched_data,192,"DEACT LOCAL");
}

static cpu_id_t def_cpus_supported(void){
  return EZA_SCHED_CPUS;
}

static status_t def_add_cpu(cpu_id_t cpu)
{
  eza_sched_cpudata_t *cpudata;
  status_t r;
 
  if(cpu >= EZA_SCHED_CPUS || sched_cpu_data[cpu] != NULL) {
    return -EINVAL;
  }

  cpudata = allocate_cpu_sched_data(cpu);
  if( cpudata == NULL ) {
    panic( "Can't allocate data for CPU N%d\n",cpu );
    return -ENOMEM;
  }

  LOCK_CPU_ARRAY();

  if( sched_cpu_data[cpu] == NULL ) {
    sched_cpu_data[cpu] = cpudata;
    r = 0;
  } else {
    r = -EEXIST;
  }

  UNLOCK_CPU_ARRAY();

  if( r != 0 ) {
    free_cpu_sched_data( cpudata );
  }

  return r;
}

static void def_scheduler_tick(void)
{
  task_t *current = current_task();
  eza_sched_cpudata_t *cpudata = CPU_SCHED_DATA();
  eza_sched_taskdata_t *tdata;
  sched_discipline_t discipl;

  if(cpudata == NULL) {
    return;
  }

  /* Idle task ?  */
  if( !current->pid ) {
    update_idle_tick_statistics(cpudata->stats);
    return;
  }

  tdata = EZA_TASK_SCHED_DATA(current);
  if( !tdata ) {
    return;
  }

  discipl = tdata->sched_discipline;

  if( discipl == SCHED_RR ) {
    if( !--tdata->time_slice ) {
      __remove_task_from_array(cpudata->active_array,current);
      __recalculate_timeslice_and_priority(current);
      __add_task_to_array(cpudata->active_array,current);
      /*kprintf( "** CPU %d: TIMESLICE IS OVER ! NEXT TIMESLICE: %d\n",
        cpu_id(), tdata->time_slice );*/
      sched_set_current_need_resched();
    }
  } else if( discipl == SCHED_FIFO ) {
  } else {
  }
}

/* NOTE: Currently works only for current task ! */
static status_t def_del_task(task_t *task)
{
  status_t r;
  eza_sched_taskdata_t *sdata;

  if( task != current_task() ) {
    return -EINVAL;
  }

  interrupts_disable();
  LOCK_TASK_STRUCT(task);

  if( task->scheduler != &eza_default_scheduler || !task->sched_data ) {
    r=-EINVAL;
    UNLOCK_TASK_STRUCT(task);
    interrupts_enable();
  } else {
    gc_action_t *action;
    eza_sched_cpudata_t *sched_data = CPU_SCHED_DATA();

    /* First, remove this task from all arrays. */
    sdata=EZA_TASK_SCHED_DATA(task);
    __remove_task_from_array(sdata->array,task);
    task->sched_data=NULL;
    UNLOCK_TASK_STRUCT(task);
    interrupts_enable();

    /* Phase 1: Free scheduler private data. */
    if( sdata ) {
      free_task_sched_data(sdata);
    }

    /* Phase 2: Prepare deffered actions. */
    action=gc_allocate_action(cleanup_thread_data,task,0);

    /* Now we can schedule deffered resource freeing. */
    preempt_disable();
    interrupts_disable();
    LOCK_TASK_STRUCT(task);

    sdata=task->sched_data;
    task->scheduler=NULL;
    sched_data->stats->active_tasks--;
    sched_set_current_need_resched();

    UNLOCK_TASK_STRUCT(task);
    interrupts_enable();

    if( action ) {
      gc_schedule_action(action);
    }
    /* No way to return - we're leaving the CPU forever ... */
    preempt_enable();
  }

  return r;
}

static status_t def_add_task(task_t *task)
{
  cpu_id_t cpu = cpu_id();
  eza_sched_taskdata_t *sdata;

  if( sched_cpu_data[cpu] == NULL || task->state != TASK_STATE_JUST_BORN ) {
    return -EINVAL;
  }

  if( task->sched_data != NULL ) {
    kprintf( KO_WARNING "def_add_task(): Task being added already attached to a scheduler !\n" );
    return -EBUSY;
  }

  if( setup_new_task(task) != 0 ) {
    return -ENOMEM;
  }

  LOCK_TASK_STRUCT(task);

  sdata = (eza_sched_taskdata_t *)task->sched_data;
  task->static_priority = EZA_SCHED_INITIAL_TASK_PRIORITY;
  task->priority = EZA_SCHED_INITIAL_TASK_PRIORITY;
  task->cpu = cpu;
  sdata->sched_discipline = SCHED_RR; /* TODO: [mt] must be SCHED_ADAPTIVE */
  sdata->max_timeslice = 0;
  sdata->array = NULL;

  UNLOCK_TASK_STRUCT(task);
  return 0;
}

long __t1;

static void def_schedule(void)
{
  eza_sched_cpudata_t *sched_data = CPU_SCHED_DATA();
  task_t *current = current_task();
  task_t *next;
  bool need_switch,ints_enabled;

  if( sched_verbose ) {
    kprintf( "ENTERING SCHEDULE STEP (%d).\n", cpu_id() );
  }
//  __READ_TIMESTAMP_COUNTER(__t1);

  /* From this moment we are in atomic context until 'arch_activate_task()'
   * finishes its job or until interrupts will be enabled in no context
   * switch is required.
   */
  ints_enabled=is_interrupts_enabled();
  if( ints_enabled ) {
    interrupts_disable();
  }

  LOCK_TASK_STRUCT(current);
  sched_reset_current_need_resched();
  LOCK_CPU_SCHED_DATA(sched_data);

  next = __get_most_prioritized_task(sched_data);
  if( next == NULL ) { /* Schedule idle task. */
    next = idle_tasks[sched_data->cpu_id];
    sched_data->stats->idle_switches++;
  }
  sched_data->stats->task_switches++;
  UNLOCK_CPU_SCHED_DATA(sched_data);

  if(current->state == TASK_STATE_RUNNING) {
    current->state = TASK_STATE_RUNNABLE;
  }

  /* Do we really need to swicth hardware context ? */
  if( next != current ) {
    need_switch = true;
  } else {
    need_switch = false;
  }

  if( next->state == TASK_STATE_RUNNABLE ) {
    next->state = TASK_STATE_RUNNING;
  }

  UNLOCK_TASK_STRUCT(current);


  if( sched_verbose ) {
    kprintf( "** [%d] RESCHED TASK: PID: %d, NEED SWITCH: %d, NEXT: %d , TS: %d **\n",
             cpu_id(), current_task()->pid, need_switch,
             next->pid, EZA_TASK_SCHED_DATA(next)->time_slice );
  }

  if( need_switch ) {
    arch_activate_task(next);
  }

  if( ints_enabled ) {
      interrupts_enable();
  }
}

static void def_reset(void)
{
  int i;

  spinlock_initialize(&cpu_data_lock);

  for(i=0;i<EZA_SCHED_CPUS;i++) {
    sched_cpu_data[i] = NULL;
  }
}

/* NOTE: task must be locked before calling this function ! */
static status_t __change_local_task_state(task_t *task,task_state_t new_state,
                                          lazy_sched_handler_t handler,void *data)
{
  status_t r = -EINVAL;
  task_state_t prev_state;
  eza_sched_cpudata_t *sched_data = CPU_SCHED_DATA();

  prev_state = task->state;
  LOCK_CPU_SCHED_DATA(sched_data);

  /* In case we're performing a 'lazy' schedule, perform some extra checks.*/
  if( handler != NULL && !handler(data) ) {
    kprintf( "[*] Breaking lazy scheduling loop.\n" );
    goto out_unlock;
  }

  if( sched_verbose ) {
    kprintf( "__change_local_task_state: STATE: %d, NEW STATE: %d\n",
             prev_state,new_state);
  }

  switch(new_state) {
   case TASK_STATE_RUNNABLE:
      if( prev_state == TASK_STATE_JUST_BORN
	  || prev_state == TASK_STATE_STOPPED ||
          prev_state == TASK_STATE_SLEEPING ) {
	__activate_local_task(task,sched_data);
        r = 0;
      }
      break;
    case TASK_STATE_STOPPED:
    case TASK_STATE_SLEEPING:
      if( task->state == TASK_STATE_RUNNABLE
	    || task->state == TASK_STATE_RUNNING ) {
	  __deactivate_local_task(task,sched_data);
          r = 0;
	}
       break;
    default:
      break;
  }

out_unlock:
  UNLOCK_CPU_SCHED_DATA(sched_data);
  UNLOCK_TASK_STRUCT(task);
  return r;
}

int sched_verbose1;

static status_t __change_task_state(task_t *task,task_state_t new_state,
                                    lazy_sched_handler_t h,void *data)
{
  status_t r;
  long is;

  if( sched_verbose1 && !cpu_id() ) {
    kprintf( "[%d] BEFORE CHANGE STATE: ATOMIC=%d,NEED RESCHED: %d, PID: %d, TASK CPU: %d\n",
             cpu_id(),in_atomic(), current_task_needs_resched(),
             task->pid,task->cpu);
  }

change_task_state:
  interrupts_save_and_disable(is);
  LOCK_TASK_STRUCT(task);

  if( task->cpu == cpu_id() ) {
    r=__change_local_task_state(task,new_state,h,data);
    interrupts_restore(is);
  } else {
    UNLOCK_TASK_STRUCT(task);
    interrupts_restore(is);

    kprintf( "[%d] BEFORE CHANGE STATE: ATOMIC=%d,NEED RESCHED: %d, PID: %d, TASK CPU: %d\n",
             cpu_id(),in_atomic(), current_task_needs_resched(),
             task->pid,task->cpu);

    r=schedule_remote_task_state_change(task,new_state);
    if( r == -EAGAIN ) {
      /* Task has changed its CPU one more time while performing our
       * request. So perform the operation yet again.
       */
      goto change_task_state;
    }
  }

  if( sched_verbose1 && !cpu_id() ) {
    kprintf( "[%d] AFTER CHANGE STATE: ATOMIC=%d,NEED RESCHED: %d, ion: %d, INTS: %d\n",
             cpu_id(),in_atomic(), current_task_needs_resched(), is,
             is_interrupts_enabled() );
  }
  cond_reschedule();
  return r;
}

status_t def_change_task_state(task_t *task,task_state_t new_state)
{
  return __change_task_state(task,new_state,NULL,NULL);
}

static status_t def_setup_idle_task(task_t *task)
{
  eza_sched_taskdata_t *sdata;

  if( setup_new_task(task) != 0 ) {
    return -ENOMEM;
  }

  sdata = (eza_sched_taskdata_t *)task->sched_data;
  task->priority = task->static_priority = EZA_SCHED_IDLE_TASK_PRIO;
  sdata->time_slice = 0;

  LOCK_TASK_STRUCT(task);
  task->state = TASK_STATE_RUNNING;
  task->scheduler = &eza_default_scheduler;
  task->sched_data = sdata;
  UNLOCK_TASK_STRUCT(task);

  return 0;
}

static void __shuffle_remote_task(task_t *target)
{
}

/* NOTE: target task must be locked ! */
static void __shuffle_task(task_t *target,eza_sched_taskdata_t *sdata, uint32_t new_prio)
{
  /* In case task is sleeping we must be able to change its priority as well */
  if (target->state != TASK_STATE_RUNNING && target->state != TASK_STATE_RUNNABLE) {
    target->priority = target->static_priority = new_prio;
    return;
  }
  if( target->cpu == cpu_id() ) {
    eza_sched_cpudata_t *sched_data = CPU_SCHED_DATA();
    task_t *mpt;

    
    LOCK_CPU_SCHED_DATA(sched_data);    

    __remove_task_from_array(sched_data->active_array,target);
    target->priority = target->static_priority = new_prio;
    kprintf("CHANGED PRIORITY (%d) TO %d\n", target->pid, target->static_priority);
    __add_task_to_array(sched_data->active_array,target);

    mpt=__get_most_prioritized_task(sched_data);
    if( mpt != NULL ) {
      if( mpt->priority < current_task()->priority ) {
        sched_set_current_need_resched();
      }
    } else {
      panic( "__shuffle_task(): No runnable tasks after adding a task !" );
    }

    UNLOCK_CPU_SCHED_DATA(sched_data);
  } else {
    __shuffle_remote_task(target);
  }
}

/* NOTE: Upon entering this routine target task is unlocked.
 */
static status_t def_scheduler_control(task_t *target,ulong_t cmd,ulong_t arg)
{
  eza_sched_taskdata_t *sdata = EZA_TASK_SCHED_DATA(target);
  bool trusted=trusted_task(target);

  switch(cmd) {
    /* Getters. */
    case SYS_SCHED_CTL_GET_MAX_TIMISLICE:
      return sdata->max_timeslice;
    case SYS_SCHED_CTL_GET_POLICY:
      return sdata->sched_discipline;

    /* Setters.  */
    case SYS_SCHED_CTL_SET_POLICY:
      if( arg == SCHED_RR || arg == SCHED_FIFO || arg == SCHED_ADAPTIVE ) {
        LOCK_TASK_STRUCT(target);
        sdata->sched_discipline = arg;
        UNLOCK_TASK_STRUCT(target);
        return 0;
      }
      return -EINVAL;
    case SYS_SCHED_CTL_SET_PRIORITY:
      if(arg <= EZA_SCHED_PRIORITY_MAX) {
        if( trusted ) {
          interrupts_disable();
          LOCK_TASK_STRUCT(target);
          
          __shuffle_task(target,sdata, arg);

          UNLOCK_TASK_STRUCT(target);
          interrupts_enable();
          cond_reschedule();
          return 0;
        } else {
          return -EPERM;
        }
      }
      return -EINVAL;
    case SYS_SCHED_CTL_SET_MAX_TIMISLICE:
      if(arg < HZ) {
        LOCK_TASK_STRUCT(target);
        sdata->max_timeslice = arg;
        UNLOCK_TASK_STRUCT(target);
        return 0;
      }
      return -EINVAL;
    case SYS_SCHED_CTL_SET_STATE:
      return def_change_task_state(target,arg);
  }
  return -EINVAL;
}

static status_t def_change_task_state_lazy(task_t *task, task_state_t state,
                                           lazy_sched_handler_t handler,
                                           void *data)
{
  return __change_task_state(task,state,handler,data);
}

static status_t def_move_task_to_cpu(task_t *task,cpu_id_t cpu) {
  eza_sched_taskdata_t *sdata = EZA_TASK_SCHED_DATA(task);
  status_t r;
  cpu_id_t mycpu=cpu_id();
  eza_sched_cpudata_t *sched_data=CPU_SCHED_DATA();
  bool active;

  if(cpu >= EZA_SCHED_CPUS || !sched_cpu_data[cpu]) {
    return -EINVAL;
  }

  __ENTER_USER_ATOMIC();
  LOCK_TASK_STRUCT(task);

  active=task->state == TASK_STATE_RUNNABLE || task->state==TASK_STATE_RUNNING;

  if( task->cpu != mycpu || !sdata || task->scheduler != &eza_default_scheduler ||
      list_node_is_bound(&task->migration_list) ) {
    r=-EINVAL;
  } else {
    r=0;
    if( cpu != mycpu ) {
      task->cpu=cpu;

      if( active ) {
        LOCK_CPU_SCHED_DATA(sched_data);
        sched_data->stats->active_tasks--;
        __remove_task_from_array(sdata->array,task);
        UNLOCK_CPU_SCHED_DATA(sched_data);

        if( task == current_task() ) {
          sched_set_current_need_resched();
        }

        task->state=TASK_STATE_STOPPED;
        r=schedule_migration(task,cpu);
      } else {
        LOCK_CPU_SCHED_DATA(sched_data);
        sched_data->stats->sleeping_tasks--;
        UNLOCK_CPU_SCHED_DATA(sched_data);
      }
    }
  }
  UNLOCK_TASK_STRUCT(task);
  __LEAVE_USER_ATOMIC();

  return r;
}

static struct __scheduler eza_default_scheduler = {
  .id = "Eza default scheduler",
  .cpus_supported = def_cpus_supported,
  .add_cpu = def_add_cpu,
  .scheduler_tick = def_scheduler_tick,
  .add_task = def_add_task,
  .del_task = def_del_task,
  .move_task_to_cpu = def_move_task_to_cpu,
  .schedule = def_schedule,
  .reset = def_reset,
  .change_task_state = def_change_task_state,
  .change_task_state_lazy = def_change_task_state_lazy,
  .setup_idle_task = def_setup_idle_task,
  .scheduler_control = def_scheduler_control,
};

scheduler_t *get_default_scheduler(void)
{
  return &eza_default_scheduler;
}

void debug_stop(uint64_t id)
{
  interrupts_disable();
  kprintf( "[[[[[[[[[[[[[[[[[[[[[[[ DEBUG STOP %d ]]]]]]]]]]]]]]]]]]\n", id );
  for( ;; );
}
