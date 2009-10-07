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
 * arch/amd64/interrupt.c: contains x86 specific routines for dealing
 *                             with hardware interrupts.
 *
 */

#include <mstring/scheduler.h>
#include <mstring/interrupt.h>
#include <mstring/time.h>
#include <arch/i8259.h>
#include <arch/apic.h>
#include <mstring/panic.h>
#include <arch/interrupt.h>
#include <mstring/smp.h>
#include <arch/smp.h>
#include <mstring/idt.h>
#include <config.h>

extern void timer_interrupt_handler(void *data);

#if 0 /* [DEACTIVATED] */
static void install_generic_irq_handlers(void)
{
  register_irq(0, timer_interrupt_handler, NULL, 0 );
}
#endif

#ifdef CONFIG_SMP
static void install_smp_irq_handlers(void)
{
  const idt_t *idt;
  long r;

  idt = get_idt();
#if 0
  r = idt->install_handler(smp_local_timer_interrupt_handler,
                           LOCAL_TIMER_CPU_IRQ_VEC);
  r |= idt->install_handler(smp_scheduler_interrupt_handler,
                            SCHEDULER_IPI_IRQ_VEC);
  if(r != 0) {
    panic( "arch_initialize_irqs(): Can't install SMP irqs !" );
  }
#endif
}
#endif /* CONFIG_SMP */

void arch_initialize_irqs(void)
{
  int idx, r;
  const idt_t *idt;
  irq_t base;

  idt = get_idt();
  
  /* Initialize the PIC */
  i8259a_init();

#ifdef CONFIG_APIC
  fake_apic_init();
#endif

  /* Initialize all possible IRQ handlers to stubs. */
  base=idt->first_available_vector();

#if 0
  for(idx=0;idx<idt->vectors_available();idx++) {
    vector_irq_table[idx] = idx;
		r=idt->install_handler((idt_handler_t)irq_entrypoints_array[idx],idx+base);
    if( r != 0 ) {
      panic( "Can't install IDT slot for IRQ #%d\n", idx );
    }
  }
#endif

  /* Setup all known interrupt handlers. */
#ifndef CONFIG_APIC
  install_generic_irq_handlers();  
#endif

#ifdef CONFIG_SMP
  install_smp_irq_handlers();
#endif
}
