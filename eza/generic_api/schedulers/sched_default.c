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
#include <mm/pt.h>
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

/* Our own scheduler. */
static struct __scheduler eza_default_scheduler;

/* Glabal per-CPU array. */
static eza_sched_cpudata_t *sched_cpu_data[EZA_SCHED_CPUS];
static spinlock_t cpu_data_lock;

#define LOCK_CPU_ARRAY() spinlock_lock(&cpu_data_lock)
#define UNLOCK_CPU_ARRAY() spinlock_unlock(&cpu_data_lock)

#define LOCK_CPU_SCHED_DATA(d) spinlock_lock(&d->lock)
#define UNLOCK_CPU_SCHED_DATA(d) spinlock_unlock(&d->lock)

#define CPU_SCHED_DATA() sched_cpu_data[cpu_id()]

#define EZA_TASK_PRIORITY(t) ((eza_sched_taskdata_t *)t->sched_data)->priority
#define EZA_TASK_SCHED_DATA(t) ((eza_sched_taskdata_t *)t->sched_data)

#define PRIO_TO_TIMESLICE(p) (p*5)

static eza_sched_taskdata_t *allocate_task_sched_data(void)
{
  /* TODO: [mt] Allocate memory via slabs !!!  */
  page_frame_t *page = alloc_page(AF_PGP);  
  return (eza_sched_taskdata_t *)pframe_to_virt(page);
}

static void free_task_sched_data(eza_sched_taskdata_t *data)
{
  /* TODO: [mt] Free structure via slabs ! */
}

static eza_sched_cpudata_t *allocate_cpu_sched_data(cpu_id_t cpu) {
  /* TODO: [mt] Allocate memory via slabs !!!  */
  page_frame_t *page = alloc_page(AF_PGP);
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

  spinlock_initialize(&cpudata->lock, "Eza scheduler runqueue lock");
  list_init_head(&cpudata->non_active_tasks);
  cpudata->active_array = &cpudata->arrays[0];

  /* Initialize scheduler statistics for this CPU. */
  cpudata->stats = &swks.cpu_stat[cpu].sched_stats;
  cpudata->cpu_id = cpu;

  for( arr=0; arr<EZA_SCHED_NUM_ARRAYS; arr++ ) {
    eza_sched_prio_array_t *array = &cpudata->arrays[arr];
    list_head_t *lh = &array->queues[0];

    memset(&array->bitmap[0], EZA_SCHED_BITMAP_PATTERN, sizeof(array->bitmap));

    for( i=0; i<EZA_SCHED_TOTAL_PRIOS; i++ ) {
      list_init_head(lh);
      lh++;
    }
  }
}

static status_t setup_new_task(task_t *task)
{
  eza_sched_taskdata_t *sdata = allocate_task_sched_data();

  if( sdata == NULL ) {
    return -ENOMEM;
  }

  list_init_node(&sdata->runlist);

  LOCK_TASK_STRUCT(task);
  task->sched_data = sdata;
  task->scheduler = &eza_default_scheduler;
  UNLOCK_TASK_STRUCT(task);
  return 0;
}

/* NOTE: This function relies on _current_ state of the task being processed.
 */
static inline void __recalculate_timeslice_and_priority(task_t *task)
{
  eza_sched_taskdata_t *tdata = EZA_TASK_SCHED_DATA(task);
  
  /* Recalculate priority. */
  tdata->priority = tdata->static_priority;

  switch(task->state) {
    case TASK_STATE_SLEEPING:
      break;
    default:
      break;
  }
  
  /* Recalculate timeslice. */
  tdata->time_slice = PRIO_TO_TIMESLICE(tdata->priority);
}

/* NOTE: Task must be locked !
 */
static inline void __reschedule_task(task_t *task)
{
  if( task->cpu == cpu_id() ) {
    kprintf( "** LOCAL NEED RESCHED: %d\n", cpu_id() );
    sched_set_current_need_resched();
  } else {
    kprintf( "** REMOTE NEED RESCHED: CPU %d, TASK's CPU: \n", cpu_id(), task->cpu );
  }
}

/* NOTE: Task and CPUdata must be locked prior to calling this function !
 */
static inline void __move_to_non_active_array(eza_sched_cpudata_t *sched_data,task_t *task)
{
  //  list_del();
}

/* Task must be locked while calling this function !
 */
static inline status_t __activate_task(task_t *task, eza_sched_cpudata_t *sched_data)
{
  eza_sched_taskdata_t *cdata = (eza_sched_taskdata_t *)current_task()->sched_data;

  __recalculate_timeslice_and_priority(task);
  task->state = TASK_STATE_RUNNABLE;
  __add_task_to_array(sched_data->active_array,task);

  kprintf( "++ NEW PRIO: %d, CURRENT: %p, CRRENT PRIO: %d\n", EZA_TASK_PRIORITY(task), current_task(), cdata->priority );
  if( EZA_TASK_PRIORITY(task) < cdata->priority ) {
    __reschedule_task(current_task());
  }

  return 0;
}

/* Task must be locked while calling this function !
 */
static inline status_t __deactivate_task(task_t *task, eza_sched_cpudata_t *sched_data)
{
  /* In case target task is running, we must reschedule it. */
  if( task->state == TASK_STATE_RUNNING ) {
  //      reschedule_task(task);
  } else { /* TASK_STATE_RUNNABLE */
    
  }

  return 0;
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

static void def_scheduler_tick1(void)
{
}

static void def_scheduler_tick(void)
{
  task_t *current = current_task();
  eza_sched_cpudata_t *cpudata = CPU_SCHED_DATA();
  eza_sched_taskdata_t *tdata;
  sched_discipline_t discipl;
  
  /* Idle task ?  */
  if( !current->pid ) {
    update_idle_tick_statistics(cpudata->stats);
    return;
  }

  tdata = EZA_TASK_SCHED_DATA(current);
  discipl = tdata->sched_discipline;

  if( discipl == SCHED_RR ) {
    if( !--tdata->time_slice ) {
      kprintf( "** TIMESLICE IS OVER !\n" );
      __remove_task_from_array(cpudata->active_array,current);
      __recalculate_timeslice_and_priority(current);
      __add_task_to_array(cpudata->active_array,current);
      kprintf( "** NEW TIMESLICE: %d\n", tdata->time_slice );
      sched_set_current_need_resched();
    }
  } else if( discipl == SCHED_FIFO ) {
  } else {
  }
}

static status_t def_add_task(task_t *task)
{
  cpu_id_t cpu = cpu_id();
  eza_sched_taskdata_t *sdata;
  eza_sched_cpudata_t *sched_data = CPU_SCHED_DATA();

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
  LOCK_CPU_SCHED_DATA(sched_data);

  sdata = (eza_sched_taskdata_t *)task->sched_data;
  sdata->static_priority = EZA_SCHED_INITIAL_TASK_PRIORITY;
  sdata->priority = EZA_SCHED_INITIAL_TASK_PRIORITY;
  task->cpu = cpu;
  sdata->task = task;
  sdata->sched_discipline = SCHED_RR; /* TODO: [mt] must be SCHED_ADAPTIVE */

  UNLOCK_CPU_SCHED_DATA(sched_data);
  UNLOCK_TASK_STRUCT(task);

  return 0;
}

static void def_schedule(void)
{
  eza_sched_cpudata_t *sched_data = CPU_SCHED_DATA();
  task_t *current = current_task();
  task_t *next;
  bool need_switch;

  LOCK_TASK_STRUCT(current);
  LOCK_CPU_SCHED_DATA(sched_data);

  sched_reset_current_need_resched();

  next = __get_most_prioritized_task(sched_data);
  if( next == NULL ) { /* Schedule idle task. */
    next = idle_tasks[sched_data->cpu_id];
    sched_data->stats->idle_switches++;
  }
  sched_data->stats->task_switches++;

  next->state = TASK_STATE_RUNNING;

  if( next != current ) {
    need_switch = true;
  } else {
    need_switch = false;
  }

  UNLOCK_CPU_SCHED_DATA(sched_data);
  UNLOCK_TASK_STRUCT(current);

  kprintf( "** RESCHEDULING TASK: PID: %d, NEED SWITCH: %d, NEXT: %d , timeslice: %d **\n",
           current_task()->pid, need_switch,
           next->pid, EZA_TASK_SCHED_DATA(next)->time_slice );

  if( need_switch ) {
    arch_activate_task(next);
  }
}

static void def_reset(void)
{
  int i;

  spinlock_initialize(&cpu_data_lock, "Eza def scheduler lock");

  for(i=0;i<EZA_SCHED_CPUS;i++) {
    sched_cpu_data[i] = NULL;
  }
}

static status_t def_change_task_state(task_t *task,task_state_t new_state)
{
  status_t r = -EINVAL;
  task_state_t prev_state;
  eza_sched_cpudata_t *sched_data = CPU_SCHED_DATA();

  LOCK_TASK_STRUCT(task);
  prev_state = task->state;
  LOCK_CPU_SCHED_DATA(sched_data);

  switch(new_state) {
    case TASK_STATE_RUNNABLE:
      if( prev_state == TASK_STATE_JUST_BORN
	  || prev_state == TASK_STATE_STOPPED) {
	r = __activate_task(task,sched_data);
      }
      break;
    case TASK_STATE_STOPPED:
        if( task->state == TASK_STATE_RUNNABLE
	    || task->state == TASK_STATE_RUNNING ) {
	  r = __deactivate_task(task,sched_data);
	}
       break;
    default:
      break;
  }

  UNLOCK_CPU_SCHED_DATA(sched_data);
  UNLOCK_TASK_STRUCT(task);
  return r;
}

static status_t def_setup_idle_task(task_t *task)
{
  eza_sched_taskdata_t *sdata;

  if( setup_new_task(task) != 0 ) {
    return -ENOMEM;
  }

  sdata = (eza_sched_taskdata_t *)task->sched_data;
  sdata->priority = sdata->static_priority = EZA_SCHED_IDLE_TASK_PRIO;
  sdata->time_slice = 0;

  LOCK_TASK_STRUCT(task);
  task->state = TASK_STATE_RUNNING;
  task->scheduler = &eza_default_scheduler;
  task->sched_data = sdata;
  UNLOCK_TASK_STRUCT(task);

  return 0;
}

static struct __scheduler eza_default_scheduler = {
  .id = "Eza default scheduler",
  .cpus_supported = def_cpus_supported,
  .add_cpu = def_add_cpu,
  .scheduler_tick = def_scheduler_tick,
  .add_task = def_add_task,
  .schedule = def_schedule,
  .reset = def_reset,
  .change_task_state = def_change_task_state,
  .setup_idle_task = def_setup_idle_task,
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
