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

#define PAGE_WIDTH    12
#define PAGE_SIZE     (1 << PAGE_WIDTH)
#define PAGE_MASK     (PAGE_SIZE - 1)

#ifdef __ASM__
#define KERNEL_BASE   0xffffffff80000000
#define KERNEL_OFFSET 0xffff800000000000

#define k2p(p)  ((p) - KERNEL_BASE)
#define p2k(p)  ((p) + KERNEL_BASE)

#else
#include <eza/arch/types.h>

#define KERNEL_BASE   0xffffffff80000000UL
#define KERNEL_OFFSET 0xffff800000000000UL

#define k2p(p)       _k2p((uintptr_t)p)
#define p2k_code(p)  (((uintptr_t)(p)) + KERNEL_BASE)
#define p2k(p)       (((uintptr_t)(p)) + KERNEL_OFFSET)

static inline uintptr_t _k2p(uintptr_t p)
{
  if(p > KERNEL_BASE)
    return p - KERNEL_BASE;
  else
    return p - KERNEL_OFFSET;
}

#endif /* __ASM__ */
<<<<<<< HEAD:include/eza/amd64/page.h
#endif /* __ARCH_PAGE_H__ */
=======

/* varios stuff for paging and tss operating
 * for more portability we assume that
 * there are no segmantation in long mode
 */

#define IDT_ITEMS  256  /* interrupt descriptors */
#define GDT_ITEMS  10   /* GDT */
#define LDT_ITEMS  2    /* Default number of LDT entries (must include 'nil'
                         * selector), so by default each task has only one
                         * LDT item.
                         */
#define PTD_SELECTOR  1  /* LDT selector that refers to per-task data. */

#define NIL_DES  0  /* nil(null) descriptor */
#define KTEXT_DES    1 /* kernel space */
#define KDATA_DES    2
#define UDATA_DES    3 /* user space */
#define UTEXT_DES    4
#define KTEXT32_DES  5 /* it's requered while bootstrap in 32bit mode */
#define TSS_DES      6
#define LDT_DES      8

#define PL_KERNEL  0
#define PL_USER    3

#define AR_PRESENT    (1<<7)  /* avialable*/
#define AR_DATA       (2<<3)
#define AR_CODE       (3<<3)
#define AR_WRITEABLE  (1<<1)
#define AR_READABLE   (1<<1)
#define AR_TSS        (0x9)   /* task state segment */
#define AR_INTR       (0xe)   /* interrupt */
#define AR_TRAP       (0xf)
#define AR_LDT        (0x2)   /* 64-bit LDT descriptor */

#define DPL_KERNEL  (PL_KERNEL<<5)
#define DPL_USPACE  (PL_USER<<5)

#define TSS_BASIC_SIZE  104
#define TSS_IOMAP_SIZE  ((4*4096)+1)  /* 16k&nil for mapping */
#define TSS_DEFAULT_LIMIT  (TSS_BASIC_SIZE-1)
#define TSS_IOPORTS_PAGES  1
#define TSS_IOPORTS_LIMIT  (PAGE_SIZE-1)

#define IO_PORTS        (IDT_ITEMS*1024)

/* bootstrap macros */
#define idtselector(des)  ((des) << 4)
#define gdtselector(des)  ((des) << 3)

/* Macros for defining task selectors. */
#define USER_SELECTOR(s) (gdtselector(s) | PL_USER)
#define KERNEL_SELECTOR(s) gdtselector(s)

#endif /* __AMD64_PAGE_H__ */
>>>>>>> zzz:include/eza/amd64/page.h

