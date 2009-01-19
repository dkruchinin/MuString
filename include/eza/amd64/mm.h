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
 * (c) Copyright 2008 Dan Kruchinin <dan.kruchinin@gmail.com>
 *
 * include/amd64/mm.h - Architecture-dependend(amd64-specific) memory-management
 *                      defenitions and constants
 *
 */


#ifndef __ARCH_MM_H__
#define __ARCH_MM_H__ 

#include <ds/iterator.h>
#include <mm/page.h>
#include <eza/arch/e820map.h>
#include <eza/arch/boot.h>
#include <mlibc/types.h>

extern int _kernel_end;
extern int _kernel_start;
extern uintptr_t _kernel_first_free_addr;
extern uintptr_t _user_va_start;
extern uintptr_t _user_va_end;

#define KERNEL_FIRST_FREE_ADDRESS (PAGE_ALIGN(_kernel_first_free_addr))
#define KERNEL_START_ADDRESS      ((uintptr_t)&_kernel_start)
#define KERNEL_END_ADDRESS        ((uintptr_t)&_kernel_end)
#define USPACE_VA_START           _user_va_start
#define USPACE_VA_END             _user_va_end
#define IDENT_MAP_PAGES           (_mb2b(2) >> PAGE_WIDTH)

#define INVALID_ADDRESS (~0UL)
#define __user

static inline bool is_kernel_addr(void *a)
{
  uintptr_t addr = (uintptr_t)a;

  if (addr >= KERNEL_START_ADDRESS) {
    return (addr <= KERNEL_FIRST_FREE_ADDRESS);
  }
  return ((k2p(addr) >= (uintptr_t)(BOOT_OFFSET - AP_BOOT_OFFSET)) &&
          (addr < KERNEL_START_ADDRESS));
}

static inline uintptr_t cut_from_usr_top_va(page_idx_t npages)
{
  _user_va_end -= (1 << npages);
  ASSERT(_user_va_end > _user_va_start);
  return _user_va_end;
}

void arch_mm_init(void);
void arch_mm_remap_pages(void);
void arch_smp_mm_init(int cpu);

#endif /* __ARCH_MM_H__ */

