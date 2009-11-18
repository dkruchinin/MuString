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
 * include/mstring/amd64/scheduler.h: AMD-specific functions for dealing with
 *                                scheduler-related information.
 */

#ifndef __ARCH_SCHEDULER_H__
#define __ARCH_SCHEDULER_H__ 

#include <config.h>
#include <arch/types.h>
#include <arch/page.h>
#include <mstring/scheduler.h>
#include <mstring/kstack.h>
#include <mstring/task.h>
#include <arch/context.h>
#include <arch/current.h>
#include <mstring/smp.h>

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
void arch_activate_task(task_t *to);

#define ARCH_CPU_RELAX
void arch_cpu_relax(void);

#endif

