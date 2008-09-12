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
 * eza/arch/amd64/interrupt.c: contains x86 specific routines for dealing
 *                             with hardware interrupts.
 *
 */

#include <eza/scheduler.h>
#include <eza/interrupt.h>
#include <eza/time.h>
#include <eza/arch/8259.h>
#include <eza/arch/ioapic.h>
#include <eza/arch/apic.h>
#include <eza/arch/mm_types.h>
#include <eza/kernel.h>

static void timer_interrupt_handler(void *data)
{
  apic_timer_hack();
  timer_tick();
  sched_timer_tick();
}

static void install_irq_handlers(void)
{
  //  register_irq( TIMER_IRQ_LINE, timer_interrupt_handler, NULL, 0 );

  register_irq(17, timer_interrupt_handler, NULL, 0 );

  //  io_apic_enable_irq(0);
}


void arch_initialize_irqs(void)
{
  int idx, r;

  /* Initialize the PIC */
  i8259a_init();
  fake_apic_init();

  /* Initialize all possible IRQ handlers to stubs. */
  for(idx=0;idx<256-IRQ_BASE;idx++) {
    r = install_interrupt_gate(idx+IRQ_BASE, irq_entrypoints_array[idx],
                               PROT_RING_0, 0 );
    if( r != 0 ) {
      panic( "Can't install IDT slot for IRQ #%d\n", idx );
    }
  }

  /* Setup all known interrupt handlers. */
  install_irq_handlers();  

}

