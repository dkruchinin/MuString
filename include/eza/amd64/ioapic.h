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
 * include/eza/amd64/ioapic.h: implements IO APIC support driver.
 *
 */

#ifndef __ARCH_IOAPIC_H__
#define __ARCH_IOAPIC_H__

#include <eza/arch/types.h>

#define IOAPIC_BASE  0xfec00000

struct __io_apic {
  uint32_t ioregsel;
} __attribute__ ((packed));



#endif /* __ARCH_IOAPIC_H__ */
