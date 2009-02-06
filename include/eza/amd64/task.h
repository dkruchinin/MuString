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
 * include/amd64/task.h: AMD64-specific tasks-related constants.
 */

#ifndef __ARCH_TASK_H__
#define __ARCH_TASK_H__ 

#include <eza/smp.h>
#include <eza/arch/current.h>
#include <eza/arch/asm.h>
#include <eza/task.h>
#include <eza/arch/cpu.h>

extern cpu_sched_stat_t PER_CPU_VAR(cpu_sched_stat);

#define arch_activate_idle_task(cpu) \
  { register task_t *t = idle_tasks[cpu]; \
    write_msr(AMD_MSR_GS,0); \
    write_msr(AMD_MSR_GS_KRN,(uint64_t)raw_percpu_get_var(cpu_sched_stat,cpu)); \
    __asm__ __volatile__( "swapgs" );                                           \
    load_stack_pointer(t->kernel_stack.high_address-128);                       \
  }

#if 0 /* [DEACTIVATED] */
static regs_t *arch_get_syscall_stack_frame(task_t *caller)
{
    return (regs_t *)(caller->kernel_stack.high_address-sizeof(regs_t));
}
#endif
#endif

