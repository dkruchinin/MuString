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

extern void initialize_idle_tasks(void);

cpu_id_t online_cpus;

void initialize_scheduler(void)
{
  initialize_kernel_stack_allocator();
  initialize_task_subsystem();
  initialize_idle_tasks();
}

void scheduler_tick(void)
{
//  task_t *current = arch_current_task();
//  cpu_sched_meta_data_t *meta_data = cpu_var(cpu_metadata,cpu_sched_meta_data_t);

//  if( current == meta_data->idle_task ) {
//    return;
//  }
}

void schedule(void)
{
//  task_t *current = current_task();
// task_t *next = NULL;

//  arch_activate_task(next);
}

