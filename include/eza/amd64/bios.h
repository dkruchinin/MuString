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
 * (c) Copyright 2005,2008 Tirra <tirra.newly@gmail.com>
 *
 * include/eza/amd64/bios.h: bios routines and defines
 *
 */

#ifndef __ARCH_BIOS_H__
#define __ARCH_BIOS_H__

#include <eza/arch/page.h>
#include <mlibc/types.h>

#define LAST_BIOS_PAGE (BIOS_END_ADDR >> PAGE_WIDTH)
#define BIOS_EBDA_P    0x40e

extern uintptr_t ebda;
extern void arch_bios_init(void);

#endif /* __ARCH_BIOS_H__ */

