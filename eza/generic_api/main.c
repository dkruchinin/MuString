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
 * (c) Copyright 2005,2008 Tirra <tirra.newly@gmail.com>
 *
 * eza/generic_api/main.c: main routine, this functions called after bootstrap
 *                          initialization made
 *
 */

#include <mm/slab.h>
#include <eza/arch/page.h>
#include <eza/arch/cpu.h>
#include <eza/kconsole.h>
#include <eza/kernel.h>
#include <eza/context.h>
#include <eza/mm_init.h>
#include <mlibc/kprintf.h>
#include <profile.h>
#include <server.h>
#include <align.h>
#include <misc.h>
#include <version.h>
#include <eza/smp.h>
#include <eza/arch/task.h>
#include <eza/interrupt.h>
#include <eza/scheduler.h>
#include <eza/arch/fault.h>
#include <eza/arch/platform.h>
#include <eza/swks.h>
#include <eza/arch/scheduler.h>
#include <eza/arch/preempt.h>
#include <eza/arch/smp.h>
#include <eza/arch/apic.h>
#include <eza/arch/atomic.h>
#include <eza/arch/mm.h>
#include <ipc/port.h>
#include <eza/resource.h>
#include <eza/arch/interrupt.h>
#include <eza/gc.h>
#include <eza/arch/mm.h>

init_t init={ /* initially created for userspace task, requered for servers loading */
   .c=0
};

/* current context safe */
context_t crsc;

extern void initialize_common_hardware(void);
extern void initialize_timer(void);

static void main_routine_stage1(void)
{
    /* Initialize PICs and setup common interrupt handlers. */
  set_cpu_online(0,1);  /* We're online. */
  sched_add_cpu(0);

  initialize_ipc();
  initialize_gc();

  arch_initialize_irqs();
  arch_specific_init();

  /* Initialize known hardware devices. */
  initialize_common_hardware();
  initialize_resources();
  /* Since the PIC is initialized, all interrupts from the hardware
   * is disabled. So we can enable local interrupts since we will
   * receive interrups from the other CPUs via LAPIC upon unleashing
   * the other CPUs.
   */

  interrupts_enable();
  initialize_swks();
  swks_add_version_info();

  /* OK, we can proceed. */
  spawn_percpu_threads();
  server_run_tasks();

  /* Enter idle loop. */
  kprintf( "CPU #0 is entering idle loop. Current task: %p, CPU ID: %d\n",
           current_task(), cpu_id() );

  idle_loop();
}

void main_routine(void) /* this function called from boostrap assembler code */
{
  kconsole_t *kcons = default_console();

  /* After initializing memory stuff, the master CPU should perform
   * the final initializations.
   */

  arch_cpu_init(0);
  install_fault_handlers();
  initialize_irqs();
  kcons->enable();
  print_kernel_version_info();
  kprintf("[MB] Modules: %d\n",init.c);
  kprintf("[LW] Initialized CPU vectors.\n");

  mm_init();

  slab_allocator_init();

  initialize_scheduler();

  initialize_timer();

  /* Now we can switch stack to our new kernel stack, setup any arch-specific
   * contexts, etc.
   */
  arch_activate_idle_task(0);
  /* Now we can continue initialization with properly initialized kernel
   * stack frame.
   */

  main_routine_stage1();
}

#ifdef CONFIG_SMP
static void main_smpap_routine_stage1(cpu_id_t cpu)
{
  install_fault_handlers();

  arch_ap_specific_init();

  /* We're online. */
  set_cpu_online(cpu,1);
  sched_add_cpu(cpu);

  interrupts_enable();

  spawn_percpu_threads();

  /* Entering idle loop. */
  kprintf( "CPU #%d is entering idle loop. Current task: %p, CPU: %d, ATOM: %d\n",
           cpu, current_task(), cpu_id(), in_atomic() );

  idle_loop();
}

void main_smpap_routine(void)
{
  static cpu_id_t cpu=1;

  kprintf("CPU#%d Hello folks! I'm here\n", cpu);

  /* Perform generic CPU initialization. Memory will be initialized later. */
  arch_cpu_init(cpu);

  /* Ramap physical memory using page directory preparead be master CPU. */
  arch_smp_mm_init(cpu);

  /* Now we can switch stack to our new kernel stack, setup any arch-specific
   * contexts, etc.
   */
  arch_activate_idle_task(cpu);
  cpu++;

  /* Continue CPU initialization in new context. */
  main_smpap_routine_stage1(1);
}
#endif

