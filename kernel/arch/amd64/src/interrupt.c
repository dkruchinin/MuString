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

#include <arch/interrupt.h>
#include <arch/seg.h>
#include <arch/i8259.h>
#include <arch/apic.h>
#include <arch/cpufeatures.h>
#include <mstring/bitwise.h>
#include <mstring/interrupt.h>
#include <mstring/panic.h>
#include <mstring/assert.h>
#include <mstring/types.h>

extern uint8_t __irqs_table[];

#define SET_IRQ_GATE(irqnum)                                        \
    idt_install_gate((irqnum) + IRQ_BASE, SEG_TYPE_INTR,            \
                     SEG_DPL_KERNEL,                                \
                     (uintptr_t)&__irqs_table + (irqnum) * 0x10, 0)

static INITCODE void register_default_irqctrl(void)
{
  struct irq_controller *irq_ctrl;

  /*
   * At first try to find out if local APIC irq
   * controller is registered
   */
  irq_ctrl = irq_get_controller(LAPIC_IRQCTRL_NAME);
  if (likely(irq_ctrl != NULL)) {
    /* If so, make it default controller */
    default_irqctrl = irq_ctrl;
    return;
  }

  /* Otherwise make i8259a default controller */
  irq_ctrl = irq_get_controller(I8259A_IRQCTRL_NAME);
  if (unlikely(irq_ctrl == NULL)) {
    panic("Can not find %s irq controller!", I8259A_IRQCTRL_NAME);
  }

  default_irqctrl = irq_ctrl;
}

INITCODE void arch_irqs_init(void)
{
  int i;

  for (i = 0; i < IRQ_VECTORS; i++) {
    SET_IRQ_GATE(i);
  }

  i8259a_init();
  if (cpu_has_feature(X86_FTR_APIC)) {
    lapic_init(0);
  }

  register_default_irqctrl();
}

