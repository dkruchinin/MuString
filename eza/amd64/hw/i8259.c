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
 * (c) Copyright 2008 Tirra <tirra.newly@gmail.com>
 *
 * eza/amd64/i8259.c: implements Intel I8259A Programmable Interrupt Controller.
 *
 */

#include <eza/interrupt.h>
#include <eza/arch/8259.h>
#include <eza/arch/types.h>
#include <eza/arch/asm.h>

static void i8259a_enable_all(void)
{
  /*enable on PIC0*/
  outb(I8259PIC0_BASE+1,0x0);
  /*enable on PIC1*/
  outb(I8259PIC1_BASE+1,0x0);
}

static void i8259a_disable_all(void)
{
  outb(I8259PIC0_BASE+1,0xff);
  outb(I8259PIC1_BASE+1,0xff);
}

static void i8259a_enable_irq(uint32_t irq)
{
  uint8_t mask;

  if(irq<0x08) {
    mask=inb(I8259PIC0_BASE+1);
    mask &= ~(1 << irq);
    outb(I8259PIC0_BASE+1,mask);
  } else { /*located in PIC1*/
    mask=inb(I8259PIC1_BASE+1);
    mask &= ~(1 << irq);
    outb(I8259PIC1_BASE+1,mask);
  }
}

static void i8259a_disable_irq(uint32_t irq)
{
  uint8_t mask;

  if(irq<0x08) {
    mask=inb(I8259PIC0_BASE+1);
    mask |= (1 << irq);
    outb(I8259PIC0_BASE+1,mask);
  } else { /*located in PIC1*/
    mask=inb(I8259PIC1_BASE+1);
    mask |= (1 << irq);
    outb(I8259PIC1_BASE+1,mask);
  }
}

static void i8259a_ack_irq(uint32_t irq) /*end of irq*/
{
  if( irq >= 8 ) {
    outb(I8259PIC1_BASE,PIC_EOI);
  }
  /* Need to acknowledge the master too. */
  outb(I8259PIC0_BASE,PIC_EOI);
}

static bool i8259a_handles_irq(uint32_t irq)
{
  return ( irq < 16 );
}


static hw_interrupt_controller_t i8259A_pic = {
  .descr = "I8259A",
  .handles_irq = i8259a_handles_irq,
  .enable_all = i8259a_enable_all,
  .disable_all = i8259a_disable_all,
  .enable_irq = i8259a_enable_irq,
  .disable_irq = i8259a_disable_irq,
  .ack_irq = i8259a_ack_irq,
};

static void initialize_pic(void)
{
  /*
   * ICW1 to set: 
   * 0x10 (offset) | ICW4 is need | cascade mode | edge trigger = 0x11
   */
  outb(I8259PIC0_BASE,0x11);
  /*
   * ICW2 to set: 8086 mode (according to intel's manual) 
   * whether irq0 maps to irq base (pin0->base, pin1->base+1, ...)
   */
  outb(I8259PIC0_BASE+1,0x20);
  /*
   * ICW3 to set: IRs set to 1 (have a slave)
   * it's mean that slave connected to slave pin irq
   */
  outb(I8259PIC0_BASE+1,0x4);
  /*
   * ICW4 to set: 0x01 for 8086 mode
   */
  outb(I8259PIC0_BASE+1,0x01);

  /*setting slave PIC*/
  outb(I8259PIC1_BASE,0x11);
  /*the same for PIC0, excluding offset to map next 8 irqs*/
  outb(I8259PIC1_BASE+1,0x28);
  /*ICW3: slave*/
  outb(I8259PIC1_BASE+1,0x02);
  /*ICW4 to set: 0x01 for 8086 mode*/
  outb(I8259PIC1_BASE+1,0x01);

  /* Disable all irqs by default. */
  i8259a_disable_all();
}

void i8259a_init(void)
{
  initialize_pic();
  register_hw_interrupt_controller( &i8259A_pic );
}

