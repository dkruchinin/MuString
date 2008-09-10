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
 * include/amd64/task.h: AMD64-specific tasks-related constants.
 */

#ifndef __ARCH_TASK_H__
#define __ARCH_TASK_H__ 

#include <eza/smp.h>
#include <eza/arch/current.h>
#include <eza/arch/asm.h>
#include <eza/scheduler.h>
#include <eza/arch/cpu.h>

typedef enum __task_privilege {
  TPL_KERNEL = 0,  /* Kernel task - the most serious level. */
  TPL_USER = 1,    /* User task - the least serious level */
} task_privelege_t;

EXTERN_PER_CPU(cpu_sched_stat,cpu_sched_stat_t);

#define arch_activate_idle_task(cpu) \
  { register task_t *t = idle_tasks[cpu]; \
    write_msr(AMD_MSR_GS,__raw_cpu_var(cpu_sched_stat,cpu_sched_stat_t,cpu) ); \
    load_stack_pointer(t->kernel_stack.high_address-128); \
  }

#endif

