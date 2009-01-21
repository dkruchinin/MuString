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
#include <eza/arch/cpu.h>
#include <mlibc/types.h>

extern int _kernel_end;
extern int _kernel_start;
extern uintptr_t _kernel_first_free_addr;
extern uintptr_t _user_va_start;
extern uintptr_t _user_va_end;
extern uintptr_t _kernel_base;

#define MIN_MEM_REQUIRED   _mb2b(32)
#define MIN_PAGES_REQUIRED (MIN_MEM_REQUIRED >> PAGE_WIDTH)

#define KERNEL_FIRST_FREE_ADDRESS (PAGE_ALIGN(_kernel_first_free_addr))
#define KERNEL_START_ADDRESS      ((uintptr_t)&_kernel_start)
#define KERNEL_END_ADDRESS        ((uintptr_t)&_kernel_end)
#define USPACE_VA_START           _user_va_start
#define USPACE_VA_END             _user_va_end
#define IDENT_MAP_PAGES           (_mb2b(2) >> PAGE_WIDTH)
#define KERNEL_INVALID_ADDRESS    0x100  /* Address that is never mapped. */
#define KERNEL_PHYS_START         0x100000
#define MIN_PHYS_MEMORY_REQUIRED  0x1800000
#define USPACE_VA_BOTTOM          (1UL << 40UL) /* 16 Terabytes */
#define USPACE_VA_TOP             0x1001000UL
#define USER_VASPACE_SIZE         (_user_va_end - _user_va_start)
#define INVALID_ADDRESS           (~0UL)

DEFINE_ITERATOR_CTX(page_frame, PF_ITER_ARCH,
                    e820memmap_t *mmap;
                    uint32_t e820id;
                    );

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

/* CR3 management. See manual for details about 'PCD' and 'PWT' fields. */
static inline void load_cr3(uintptr_t phys_addr, uint8_t pcd, uint8_t pwt)
{
  uintptr_t cr3_val = (((pwt & 1) << 3) | ((pcd & 1) << 4));
  
  /* Normalize new PML4 base. */
  phys_addr >>= PAGE_WIDTH;

  /* Setup 20 lowest bits of the PML4 base. */
  cr3_val |= ((phys_addr & 0xfffff) << PAGE_WIDTH);

  /* Setup highest 20 bits of the PML4 base. */
  cr3_val |= ((phys_addr & (uintptr_t)0xfffff00000) << PAGE_WIDTH);
  
  __asm__ volatile("movq %0, %%cr3" :: "r" (cr3_val));
}

void arch_mm_init(void);
void arch_mm_remap_pages(void);
void arch_smp_mm_init(cpu_id_t cpu);
void arch_mm_stage0_init(cpu_id_t cpu);

#endif /* __ARCH_MM_H__ */

