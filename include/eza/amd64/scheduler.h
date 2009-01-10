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

#include <config.h>
#include <eza/arch/types.h>
#include <eza/arch/page.h>
#include <eza/scheduler.h>
#include <eza/kstack.h>
#include <eza/amd64/context.h>
#include <eza/arch/current.h>
#include <eza/smp.h>
#include <eza/arch/mm_types.h>

static inline cpu_id_t cpu_id(void)
{
  cpu_id_t c;

  if( online_cpus != 0 ) {
    read_css_field(cpu,c);
  } else {
    c = 0;
  }

  return c;  
}

void arch_hw_activate_task(arch_context_t *new_ctx, task_t *new_task,
                           arch_context_t *old_ctx, uintptr_t kstack);

/* NOTE: Interrupts must be disabled before calling this function. */
static inline void arch_activate_task(task_t *to)
{
  arch_context_t *to_ctx = (arch_context_t*)&to->arch_context[0];
  arch_context_t *from_ctx = (arch_context_t*)&(current_task()->arch_context[0]);
  tss_t *tss=to_ctx->tss;
  uint16_t tss_limit;

  if( !tss ) {
    tss=get_cpu_tss(to->cpu);
    tss_limit=TSS_DEFAULT_LIMIT;
  } else {
    tss_limit=to_ctx->tss_limit;
  }

  /* We should setup TSS to reflect new task's kernel stack. */
  tss->rsp0 = to->kernel_stack.high_address;
  /* Reload TSS. */
  load_tss(to->cpu,tss,tss_limit);

  /* Setup LDT for new task. */
  if( to_ctx->ldt ) {
    load_ldt(to->cpu,to_ctx->ldt,to_ctx->ldt_limit);
    /* Load all 64 bits of per-task data via MSR to FS. */
    if( to_ctx->per_task_data ) {
      kprintf( "* WRITING FS MSR: %p\n",to_ctx->per_task_data );
      write_msr(AMD_MSR_FS,to_ctx->per_task_data);
      kprintf( "* DONE !\n" );
    }
  }

  /* Let's jump ! */
//#ifdef CONFIG_TEST
  kprintf( "** ACTIVATING TASK: 0x%X:0x%X (CPU: %d). FS=0x%X\n",
           to->pid,to->tid,to->cpu,to_ctx->fs );
//#endif

  arch_hw_activate_task(to_ctx,to,from_ctx,to->kernel_stack.high_address);
}

#endif

