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
 * include/eza/amd64/scheduler.h: AMD-specific functions for dealing with
 *                                scheduler-related information.
 */


#ifndef __ARCH_SCHEDULER_H__
#define __ARCH_SCHEDULER_H__ 

#include <eza/arch/types.h>
#include <eza/arch/page.h>
#include <eza/scheduler.h>
#include <eza/kstack.h>
#include <eza/amd64/context.h>

static inline task_t *arch_current_task(void)
{
  kernel_task_data_t *t;

  __asm__ volatile( "movq %%rsp, %%rbx\n"
                    "andq %%rax, %%rbx\n"
                     : "=b" (t) : "a" (KERNEL_STACK_MASK) );
  return &t->task;
}

static inline system_sched_data_t *arch_system_sched_data(void)
{
  kernel_task_data_t *t;

  __asm__ volatile( "movq %%rsp, %%rbx\n"
                    "andq %%rax, %%rbx\n"
                     : "=b" (t) : "a" (KERNEL_STACK_MASK) );
  return &t->system_data;
}

void arch_hw_activate_task( uintptr_t sp, uintptr_t cr3, uintptr_t *saved_sp );

static inline void arch_activate_task(task_t *to)
{
  task_t *from = current_task();
  arch_context_t *from_ctx = (arch_context_t*)&from->arch_context[0];
  arch_context_t *to_ctx = (arch_context_t*)&to->arch_context[0];

  /* Let's jump ! */
  arch_hw_activate_task(to_ctx->cr3,to_ctx->rsp,&from_ctx->rsp);
}

#endif

