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
 *
 */

#ifndef __MSTRING_ARCH_PAGE_H__
#define __MSTRING_ARCH_PAGE_H__

/* Physical address where kernel starts from. */
#define KPHYS_START   0x100000
#define PAGE_SIZE     4096
#define PAGE_MASK     (PAGE_SIZE - 1)
#define PAGE_WIDTH    12

#ifdef __ASM__
#define KERNEL_OFFSET 0xffffffff80000000

#define PHYS_TO_KVIRT(phys_addr) ((phys_addr) + KERNEL_OFFSET)
#define KVIRT_TO_PHYS(virt_addr) ((virt_addr) - KERNEL_OFFSET)
#else /* __ASM__ */
#define KERNEL_OFFSET 0xffffffff80000000UL

#define PHYS_TO_KVIRT(phys_addr) ((uintptr_t)(phys_addr) + KERNEL_OFFSET)
#define KVIRT_TO_PHYS(virt_addr) ((uintptr_t)(virt_addr) - KERNEL_OFFSET)
#endif /* ! __ASM__ */

#endif /* __MSTRING_ARCH_PAGE_H__ */

