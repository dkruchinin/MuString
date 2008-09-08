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
 * include/eza/amd64/i8254.h: implements i8254 timer driver.
 *
 */

#ifndef __ARCH_I8254_H__
#define __ARCH_I8254_H__

#define I8254_BASE  0x40

#define DCLOCK  150000
#define MAGIC_CLOCKN  1194 /*black magic, blackjek ...*/

/*FIXME: HZ set outhere ... */
#define CLOCK_HZ  100

void i8254_init(void);
uint64_t i8254_calibrate_delay_loop(void);

#endif

