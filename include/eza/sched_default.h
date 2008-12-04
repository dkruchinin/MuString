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
 * eza/generic_api/schedulers/sched_default.c: Data types and function prototypes
 *  for EZA default scheduler.
 */


#ifndef __SCHED_DEFAULT_H__
#define  __SCHED_DEFAULT_H__

#include <eza/scheduler.h>
#include <eza/arch/types.h>
#include <eza/smp.h>
#include <ds/list.h>
#include <eza/bits.h>
#include <eza/arch/bits.h>
#include <eza/spinlock.h>

#define eza_sched_type_t uint64_t
#define EZA_SCHED_PRIO_GRANULARITY 64

#define EZA_SCHED_RT_TASKS_WIDTH 1

#define EZA_SCHED_NONRT_TASKS_WIDTH 1
#define EZA_SCHED_TOTAL_WIDTH (EZA_SCHED_RT_TASKS_WIDTH+EZA_SCHED_NONRT_TASKS_WIDTH)

#define EZA_SCHED_NONRT_PRIOS (EZA_SCHED_NONRT_TASKS_WIDTH*EZA_SCHED_PRIO_GRANULARITY)
#define EZA_SCHED_RT_PRIOS (EZA_SCHED_RT_TASKS_WIDTH*EZA_SCHED_PRIO_GRANULARITY)

#define EZA_SCHED_NONRT_PRIO_BASE EZA_SCHED_RT_PRIOS
#define EZA_SCHED_RT_PRIO_BASE 0
#define EZA_SCHED_NONRT_MIN_PRIO EZA_SCHED_NONRT_PRIO_BASE

#define EZA_SCHED_NONRT_PRIORITY_MAX (EZA_SCHED_NONRT_PRIO_BASE+EZA_SCHED_NONRT_PRIOS-1)
#define EZA_SCHED_RT_MAXPRIO (EZA_SCHED_RTPRIOS-1)
#define EZA_SCHED_TOTAL_PRIOS (EZA_SCHED_NONRT_PRIORITY_MAX+1)

#define EZA_SCHED_PRIORITY_MAX (EZA_SCHED_NONRT_PRIORITY_MAX)
#define EZA_SCHED_IDLE_TASK_PRIO (EZA_SCHED_PRIORITY_MAX+1)
#define EZA_SCHED_DEF_NONRT_PRIO (EZA_SCHED_NONRT_MIN_PRIO + EZA_SCHED_NONRT_PRIOS/2)

#define EZA_SCHED_CPUS MAX_CPUS
#define EZA_SCHED_NUM_ARRAYS 2
#define EZA_SCHED_BITMAP_PATTERN 0x00

#define EZA_SCHED_INITIAL_TASK_PRIORITY EZA_SCHED_DEF_NONRT_PRIO

/* 64-bit bits-related stuff. */
#define SET_BITMAP_BIT(array,bit) \
  set_and_test_bit_mem_64(&array->bitmap[0],bit)

#define RESET_BITMAP_BIT(array,bit) \
  reset_and_test_bit_mem_64(&array->bitmap[0],bit)

#define FIND_FIRST_BITMAP_BIT(array) \
  find_first_bit_mem_64(&array->bitmap[0],EZA_SCHED_TOTAL_WIDTH)

typedef struct __eza_sched_prio_array {
  eza_sched_type_t bitmap[EZA_SCHED_TOTAL_WIDTH];
  list_head_t queues[EZA_SCHED_TOTAL_PRIOS];
  ulong_t num_tasks;
} eza_sched_prio_array_t;

typedef struct __eze_sched_taskdata {
  spinlock_t sched_lock;
  time_slice_t time_slice;
  list_node_t runlist;
  sched_discipline_t sched_discipline;
  task_t *task;

  time_slice_t max_timeslice;
  eza_sched_prio_array_t *array;
} eza_sched_taskdata_t;

#define is_rt_task(t) (((t)->sched_discipline == SCHED_RR) ||   \
                       ((t)->sched_discipline) == SCHED_FIFO)

typedef struct __eza_sched_cpudata {
  spinlock_t lock;
  bound_spinlock_t __lock;
  scheduler_cpu_stats_t *stats;
  eza_sched_prio_array_t *active_array,*expired_array;
  eza_sched_prio_array_t arrays[EZA_SCHED_NUM_ARRAYS];
  task_t *running_task;
  cpu_id_t cpu_id;
} eza_sched_cpudata_t;

extern eza_sched_cpudata_t *sched_cpu_data[EZA_SCHED_CPUS];

/* Array must be locked prior to calling this function !
 */
static inline void __add_task_to_array(eza_sched_prio_array_t *array,task_t *task)
{
  eza_sched_taskdata_t *sched_data = (eza_sched_taskdata_t *)task->sched_data;
  priority_t prio = task->priority;

  sched_data->array = array;
  SET_BITMAP_BIT(array,prio);
  list_add2tail(&array->queues[prio],&sched_data->runlist);
  array->num_tasks++;
}

/* NOTE: Array mus be locked !
 */
static inline void __remove_task_from_array(eza_sched_prio_array_t *array,task_t *task)
{
  eza_sched_taskdata_t *sched_data = (eza_sched_taskdata_t *)task->sched_data;
  priority_t prio = task->priority;

  list_del(&sched_data->runlist);
  sched_data->array = NULL;
  array->num_tasks--;
  
  if( list_is_empty( &array->queues[prio] ) ) {
    RESET_BITMAP_BIT(array,prio);
  }
}

static inline task_t *__get_most_prioritized_task(eza_sched_cpudata_t *sched_data)
{
  eza_sched_prio_array_t *array = sched_data->active_array;
  bit_idx_t idx = FIND_FIRST_BITMAP_BIT(array);

  if( idx != INVALID_BIT_INDEX ) {
    list_head_t *lh = &array->queues[idx];
    if( !list_is_empty(lh) ) {
      eza_sched_taskdata_t *sdata = list_entry(list_node_first(lh),eza_sched_taskdata_t,runlist);
      return sdata->task;
    } else {
      RESET_BITMAP_BIT(array,idx);
    }
  }
  return NULL;
}

#define LOCK_CPU_SCHED_DATA(d)                  \
    spinlock_lock(&d->lock)

#define UNLOCK_CPU_SCHED_DATA(d)                \
  spinlock_unlock(&d->lock)

#define EZA_TASK_SCHED_DATA(t) ((eza_sched_taskdata_t *)t->sched_data)

#define __LOCK_CPU_SCHED_DATA(d)  bound_spinlock_lock_cpu(&(d)->__lock,cpu_id())
#define __UNLOCK_CPU_SCHED_DATA(d)  bound_spinlock_unlock(&(d)->__lock)

static inline eza_sched_cpudata_t *get_task_sched_data_locked(task_t *task,
                                                              ulong_t *is)
{
  while(1) {
    ulong_t cpu=task->cpu;
    eza_sched_cpudata_t *cdata=sched_cpu_data[cpu];

    interrupts_save_and_disable(*is);
    if( bound_spinlock_trylock_cpu(&cdata->__lock,cpu_id() ) ) {
      if( task->cpu == cpu ) {
        return cdata;
      } else {
        /* No luck: someone has changed task's CPU concurrently. */
        bound_spinlock_unlock(&cdata->__lock);
      }
    }
    interrupts_restore(*is);
  }
}

#endif
