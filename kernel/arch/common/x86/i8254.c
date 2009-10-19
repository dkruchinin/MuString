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
 * (c) Copyright 2008 Tirra <tirra.newly@gmail.com>
 *
 * mstring/amd64/i8254.c: implements i8254 timer driver.
 *
 */

#include <mstring/interrupt.h>
#include <mstring/timer.h>
#include <arch/types.h>
#include <arch/asm.h>
#include <arch/i8254.h>
#include <arch/i8259.h>
#include <mstring/kprintf.h>
#include <mstring/unistd.h>

#define PIT_DIVISOR ((PIT_OSC_FREQ + HZ / 2) / HZ)

static tick_t i8254_read(void)
{
  uint_t low, high;

  outb(I8254_BASE + 0x03, 0);
  low = inb(I8254_BASE);
  high = inb(I8254_BASE);
  return ((high << 8) | low);
}

static void i8254_delay(uint64_t usecs)
{
  int ticks_left, delta, tick, prev_tick;

  prev_tick = i8254_read();
  if (usecs == 0) {
    ticks_left = 0;
  }
  else if (usecs < 256) {
    ticks_left = ((uint_t)usecs * 39099 + (1 << 15) - 1) >> 15;
  }
  else {
    ticks_left = ((uint_t)usecs * (long long)PIT_OSC_FREQ + 999999)
      / 1000000;
  }
  while (ticks_left > 0) {
    tick = i8254_read();
    delta = prev_tick - tick;
    prev_tick = tick;
    if (delta < 0) {
      delta += PIT_DIVISOR;
      if (delta < 0) {
        delta = 0;
      }
    }

    ticks_left -= delta;
  }
}

static struct hwclock pit_clock = {
  .name = "i8254 PIT",
  .divisor = PIT_DIVISOR,
  .freq = PIT_OSC_FREQ,
  .read = i8254_read,
  .delay = i8254_delay,
};

static struct irq_action pit_irq = {
  .name = "PIT timer interrupt",
  .handler = timer_interrupt_handler,
};

void i8254_init(void) 
{
  struct irq_controller *irq_ctrl;
  int ret;

  outb(I8254_BASE+3,0x34);
  outb(I8254_BASE,PIT_DIVISOR & 0xff);
  outb(I8254_BASE,PIT_DIVISOR >> 8);  
  hwclock_register(&pit_clock);
  default_hwclock = &pit_clock;
  irq_ctrl = irq_get_controller(I8259A_IRQCTRL_NAME);
  if (unlikely(irq_ctrl == NULL)) {
    panic("Unable to find %s IRQ controller to register "
          "PIT timer interrupt\n", I8259A_IRQCTRL_NAME);
  }

  ret = irq_register_line_and_action(PIT_IRQ, irq_ctrl, &pit_irq);
  if (ret) {
    panic("Failed to register irq %s on irq line %d: [RET = %d]\n",
          pit_irq.name, PIT_IRQ, ret);
  }
}

