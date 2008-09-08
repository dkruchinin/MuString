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
 * include/eza/task.h: generic functions for dealing with task creation.
 */


#ifndef __TASK_H__
#define __TASK_H__ 

#include <eza/arch/types.h>
#include <eza/scheduler.h>
#include <eza/kstack.h>
#include <eza/arch/context.h>
#include <eza/arch/task.h>

typedef enum __task_creation_flag_t {
  CLONE_MM = 0x1,
} task_creation_flags_t;

typedef struct __uspace_thread_creation_data {
  uintptr_t entry_point, stack_pointer;
} uspace_thread_creation_data_t;

int setup_task_kernel_stack(task_t *task);
void initialize_task_system_data(kernel_task_data_t *task, cpu_id_t cpu);

status_t kernel_thread(void (*fn)(void *), void *data);

status_t arch_setup_task_context(task_t *newtask,task_creation_flags_t flags,
                                 task_privelege_t priv);

status_t create_task(task_t *parent,task_creation_flags_t flags,task_privelege_t priv,
                     task_t **new_task);
status_t create_new_task(task_t *parent, task_t **t, task_creation_flags_t flags,task_privelege_t priv);
void free_task_struct(task_t *task);

#endif

