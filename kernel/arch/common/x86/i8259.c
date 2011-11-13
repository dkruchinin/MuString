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
 * (c) Copyright 2008 Tirra <madtirra@jarios.org>
 *
 * eza/amd64/i8259.c: implements Intel I8259A Programmable Interrupt Controller.
 *
 */

#include <arch/asm.h>
#include <arch/i8259.h>
#include <mstring/interrupt.h>
#include <mstring/panic.h>
#include <mstring/types.h>

static void i8259a_mask_all(void)
{
  outb(I8259_PIC_MASTER + 1, 0xff);
  outb(I8259_PIC_SLAVE + 1, 0xff);
}

static void i8259a_unmask_all(void)
{
  outb(I8259_PIC_MASTER + 1, 0x0);
  outb(I8259_PIC_SLAVE + 1, 0x0);
}

static void i8259a_unmask_irq(irq_t irq)
{
  uint8_t mask;

  if(irq < 0x08) {
    mask= inb(I8259_PIC_MASTER + 1);
    mask &= ~(1 << irq);
    outb(I8259_PIC_MASTER + 1, mask);
  }
  else { /* located on PIC slave */
    mask = inb(I8259_PIC_SLAVE + 1);
    mask &= ~(1 << irq);
    outb(I8259_PIC_SLAVE + 1, mask);
  }
}

static void i8259a_mask_irq(irq_t irq)
{
  uint8_t mask;

  if(irq < 0x08) {
    mask = inb(I8259_PIC_MASTER + 1);
    mask |= (1 << irq);
    outb(I8259_PIC_MASTER + 1, mask);
  }
  else { /*located in PIC1*/
    mask = inb(I8259_PIC_SLAVE + 1);
    mask |= (1 << irq);
    outb(I8259_PIC_SLAVE + 1, mask);
  }
}

static void i8259a_ack_irq(irq_t irq) /*end of irq*/
{
  if( irq >= 8 ) {
    outb(I8259_PIC_SLAVE, 0x60 + (irq & 7));
    outb(I8259_PIC_MASTER, 0x60 + 2);
    outb(I8259_PIC_SLAVE, PIC_EOI);
  }
  else {
    outb(I8259_PIC_MASTER, 0x60 + irq);
  }

  outb(I8259_PIC_MASTER, PIC_EOI);
}

static void i8259a_set_affinity(irq_t irq, cpumask_t mask)
{
  /* stub */
}


static struct irq_controller i8259A_pic = {
  .name = I8259A_IRQCTRL_NAME,
  .mask_all = i8259a_mask_all,
  .unmask_all = i8259a_unmask_all,
  .mask_irq = i8259a_mask_irq,
  .unmask_irq = i8259a_unmask_irq,
  .ack_irq = i8259a_ack_irq,
  .set_affinity = i8259a_set_affinity
};

static void i8259_spurious_handler(void *unused)
{
  return;
}

static struct irq_action i8259_spurious = {
  .name = "PIC spurious interrupt",
  .handler = i8259_spurious_handler,
};

INITCODE void i8259a_init(void)
{
  int ret;

  /*
   * ICW1 to set:
   * 0x10 (offset) | ICW4 is need | cascade mode | edge trigger = 0x11
   */
  outb(I8259_PIC_MASTER, 0x11);

  /*
   * ICW2 to set: 8086 mode (according to intel's manual)
   * whether irq0 maps to irq base (pin0->base, pin1->base+1, ...)
   */
  outb(I8259_PIC_MASTER + 1, 0x20);

  /*
   * ICW3 to set: IRs set to 1 (have a slave)
   * it's mean that slave connected to slave pin irq
   */
  outb(I8259_PIC_MASTER + 1, 0x4);

  /*
   * ICW4 to set: 0x01 for 8086 mode
   */
  outb(I8259_PIC_MASTER + 1, 0x01);

  /* setting slave PIC */
  outb(I8259_PIC_SLAVE, 0x11);

  /* the same for PIC0, excluding offset to map next 8 irqs */
  outb(I8259_PIC_SLAVE + 1, 0x28);

  /* ICW3: slave */
  outb(I8259_PIC_SLAVE + 1, 0x02);

  /* ICW4 to set: 0x01 for 8086 mode */
  outb(I8259_PIC_SLAVE + 1, 0x01);
  i8259a_mask_all();
  irq_register_controller(&i8259A_pic);

  ret = irq_register_line_and_action(PIC_SPURIOUS_IRQ,
                                     &i8259A_pic, &i8259_spurious);
  if (ret) {
    panic("Failed to register IRQ %s for line %d: [RET = %d]\n",
          i8259A_pic.name, PIC_SPURIOUS_IRQ, ret);
  }
}

