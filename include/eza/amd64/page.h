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
 * (c) Copyright 2009 Dan Kruchinin <dk@jarios.org>
 *
 * include/eza/amd64/page.h: functions and definions for working with paging
 *                           on amd64
 *
 */

#ifndef __ARCH_PAGE_H__
#define __ARCH_PAGE_H__

#define PAGE_WIDTH               12
#define PAGE_SIZE                (1 << PAGE_WIDTH)
#define PAGE_MASK                (PAGE_SIZE - 1)
#define KERNEL_BASE              0xffffffff80000000
#define KERNEL_OFFSET            0xffff800000000000
#define KERNEL_INVALID_ADDRESS   0x100  /* Address that is never mapped. */
#define KERNEL_PHYS_START        0x100000
#define MIN_PHYS_MEMORY_REQUIRED 0x1800000
#define USPACE_VA_BOTTOM         (1UL << 40UL) /* 16 Terabytes */
#define USPACE_VA_TOP            0x1001000UL

#ifndef __ASM__
#include <eza/arch/types.h>

#define USER_VASPACE_SIZE (_user_va_end - _user_va_start)

static inline uintptr_t _k2p(uintptr_t p)
{
  if(p > KERNEL_BASE)
    return p - KERNEL_BASE;
  else
    return p - KERNEL_OFFSET;
}

#define k2p(p)       _k2p((uintptr_t)p)
#define p2k_code(p)  (((uintptr_t)(p)) + KERNEL_BASE)
#define p2k(p)       (((uintptr_t)(p)) + KERNEL_OFFSET)
#else
#define k2p(p)  ((p) - KERNEL_BASE)
#define p2k(p)  ((p) + KERNEL_BASE)
#endif /* __ASM__ */
#endif /* __ARCH_PAGE_H__ */

