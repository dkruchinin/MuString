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
 * eza/amd64/opapic.c: implements IO APIC support driver.
 *
 */

#include <eza/interrupt.h>
#include <eza/arch/8259.h>
#include <eza/arch/i8254.h>
#include <eza/arch/types.h>
#include <eza/arch/asm.h>
#include <eza/arch/apic.h>
#include <eza/arch/mm_types.h>
#include <eza/arch/interrupt.h>
#include <eza/arch/ioapic.h>
#include <mlibc/kprintf.h>
#include <mlibc/unistd.h>

/*
 * NOTE: there are too black magic,
 * my attemption was to make full support 
 * in one day
 */

volatile static struct __io_apic_t *io_apic=(struct __io_apic_t *)IOAPIC_BASE;




