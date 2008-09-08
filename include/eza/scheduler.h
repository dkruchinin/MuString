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
 * include/eza/scheduler.h: contains main types and prototypes for the generic
 *                          scheduler layer.
 *
 */

#ifndef __SCHEDULER_H__
#define __SCHEDULER_H__ 

#include <eza/arch/types.h>
#include <eza/resource.h>
#include <mm/pt.h>
#include <eza/kstack.h>

#define PRIORITY_MAX  128
#define IDLE_TASK_PRIORITY (PRIORITY_MAX+1)

#define current_task()  arch_current_task()
#define system_sched_data()  arch_system_sched_data()

typedef uint32_t time_slice_t;

typedef enum __task_state {
  TASK_STATE_JUST_BORN = 0,
  TASK_STATE_RUNNABLE = 1,
  TASK_STATE_RUNNING = 2,
  TASK_STATE_SLEEPING = 3,
  TASK_STATE_ZOMBIE = 4,
} task_state_t;

typedef uint8_t priority_t;
typedef uint32_t cpu_affinity_array_t;

#define CPU_AFFINITY_ALL_CPUS 0

typedef struct __task_struct {
  pid_t pid, ppid;
  
  priority_t static_priority, priority;
  time_slice_t time_slice;
  task_state_t state;
  cpu_affinity_array_t cpu_affinity;

  kernel_stack_t kernel_stack;
  page_directory_t page_dir;

  /* Arch-dependent context is located here */
  uint8_t arch_context[];
} task_t;


typedef struct __cpu_sched_meta_data {
  task_t *idle_task;
} cpu_sched_meta_data_t;

typedef struct __system_sched_data {
  cpu_id_t cpu_id;
  uint32_t flags;
  uint32_t irq_num;
} system_sched_data_t;

typedef struct __kernel_task_data {
  system_sched_data_t system_data;
  task_t task;
} kernel_task_data_t;


void initialize_scheduler(void);
void scheduler_tick(void);
void schedule(void);
void idle_loop(void);

extern kernel_task_data_t * idle_tasks[];

#endif

