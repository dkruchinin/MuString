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
 * (c) Copyright 2008 Tirra <tirra.newly@gmail.com>
 *
 * include/eza/amd64/8259.h: Contains base constants for the I8259A PIC.
 *
 */

#ifndef __8259_H__
#define __8259_H__

#define I8259PIC0_BASE  0x20
#define I8259PIC1_BASE  0xa0

typedef enum __i8259_common_constants {
  MAX_IRQ_NUM = 16,
} i8259_common_constants;

typedef enum __i8259_ports {
  A = 10,
} i8259_ports_t;

typedef enum __i8259_commands {
  PIC_EOI = 0x20, /* End of interrupt */
} i8259_commands;

typedef enum __i8259a_irq_lines {
  TIMER_IRQ_LINE = 0,
} i8259a_irq_lines;

void i8259a_init(void);

#endif

