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
 * (c) Copyright 2008 Tirra <tirra.newly@gmail.com>
 *
 * eza/amd64/fake_apic.c: implements fake APIC support driver.
 *
 */

#include <eza/interrupt.h>
#include <eza/arch/8259.h>
#include <eza/arch/i8254.h>
#include <eza/arch/types.h>
#include <eza/arch/asm.h>
#include <eza/arch/apic.h>
#include <eza/arch/interrupt.h>
#include <mlibc/kprintf.h>
#include <mlibc/unistd.h>

#define APIC_TIMER_MASK  (1 << 0)
#define APIC_LVTI0_MASK  (1 << 1)
#define APIC_LVTI1_MASK  (1 << 2)

/* TODO: make a full save/restore of varios APICs register intr */

static void fake_apic_enable_all(void)
{
  /*dummy*/
}

static void fake_apic_disable_all(void)
{
  /*dummy*/
}

static void fake_apic_enable_irq(uint32_t irq)
{
  /*dummy*/
}

static void fake_apic_disable_irq(uint32_t irq)
{
  /*dummy*/
}

static bool fake_apic_handles_irq(uint32_t irq)
{
  return (irq < NUM_IRQS);
}

static void local_apic_send_eoi_s(uint32_t v)
{
  local_apic_send_eoi();
}

static hw_interrupt_controller_t fake_apic_cont = {
  .descr = "FAKEAPIC",
  .handles_irq = fake_apic_handles_irq,
  .enable_all = fake_apic_disable_all,
  .disable_all = fake_apic_enable_all,
  .enable_irq = fake_apic_enable_irq,
  .disable_irq = fake_apic_disable_irq,
  .ack_irq = local_apic_send_eoi_s,
};

void fake_apic_init(void)
{
  register_hw_interrupt_controller( &fake_apic_cont );
}

