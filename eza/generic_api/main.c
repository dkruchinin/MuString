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

#include <mm/page.h>
#include <eza/arch/types.h>
#include <eza/arch/mm_types.h>
#include <eza/arch/page.h>
#include <eza/arch/cpu.h>
#include <eza/kconsole.h>
#include <eza/kernel.h>
#include <eza/context.h>
#include <eza/mm_init.h>
#include <mlibc/kprintf.h>
#include <profile.h>
#include <align.h>
#include <misc.h>
#include <eza/smp.h>
#include <eza/interrupt.h>
#include <eza/scheduler.h>
#include <eza/arch/fault.h>
#include <eza/arch/platform.h>
#include <eza/arch/task.h>
#include <eza/swks.h>
#include <eza/arch/scheduler.h>

init_t init={ /* initially created for userspace task, requered for servers loading */
   .c=0
};

/* current context safe */
context_t crsc;

extern void initialize_common_hardware(void);
extern void start_init(void);
extern void initialize_timer(void);


static void main_routine_stage1(void)
{
  /* Initialize PICs and setup common interrupt handlers. */

  arch_specific_init();
  arch_initialize_irqs();
  /* Initialize known hardware devices. */
  initialize_common_hardware();
 
  /* Since the PIC is initialized, all interrupts from the hardware
   * is disabled. So we can enable local interrupts since we will
   * receive interrups from the other CPUs via LAPIC upon unleashing
   * the other CPUs.
   */
  interrupts_enable();

  sched_add_cpu(0);
  set_cpu_online(0,1);  /* We're online. */

  initialize_swks();


  /* The other CPUs are running, the scheduler is ready, so we can
   * enable all interrupts.
   */
  enable_all_irqs();

  /* TODO: Here we should wake up all other CPUs, if any. */

  /* OK, we can proceed. */
  start_init();
 
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
  kcons->enable();
  kprintf("[LW] MuiString starts ...\n");
  /* init memory manager stuff - stage 0 */
  arch_mm_stage0_init(0);
  kprintf("[MM] Stage0 memory manager initied.\n");    
  install_fault_handlers();
  initialize_irqs();
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
  cpu_id_t c;

  install_fault_handlers();
  interrupts_enable();

  /* We're online. */
  set_cpu_online(cpu,1);

  /* Entering idle loop. */
  kprintf( "CPU #%d is entering idle loop. Current task: %p, CPU ID: %d\n",
           cpu, current_task(), cpu_id() );

  for( ;; ) {
  }
}

void main_smpap_routine(void)
{
  static cpu_id_t cpu = 1;

  kprintf("CPU#%d Hello folks! I'm here\n", cpu);

  /* Ramap physical memory using page directory preparead be master CPU. */
  arch_mm_stage0_init(cpu); 

  /* Now we can switch stack to our new kernel stack, setup any arch-specific
   * contexts, etc.
   */
  arch_activate_idle_task(cpu);

  /* Continue CPU initialization in new context. */
  cpu++;
  main_smpap_routine_stage1(1);
}
#endif

