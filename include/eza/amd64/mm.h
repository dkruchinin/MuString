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

extern int _kernel_start, _kernel_end;
extern uintptr_t __kernel_first_free_addr;
extern uintptr_t __uspace_top_vaddr;
extern uintptr_t __kernel_va_base;

#define MIN_MEM_REQUIRED    _mb2b(8)                        /**< Minimum memory required for system */
#define MIN_PAGES_REQUIRED  (MIN_MEM_REQUIRED >> PAGE_WIDTH) /**< Minimum number of pages that must be present in a system */

#define KERNEL_START_PHYS   ((uintptr_t)&_kernel_start) /**/
#define __KERNEL_END_PHYS   ((uintptr_t)&_kernel_end)
#define KERNEL_END_PHYS     (PAGE_ALIGN(__kernel_first_free_addr))
#define IDENT_MAP_PAGES     (_mb2b(2) >> PAGE_WIDTH)
#define USPACE_VA_TOP       (1UL << 40UL) /* 16 Terabytes */
#define USPACE_VA_BOTTOM    0x1001000UL
#define INVALID_ADDRESS           (~0UL)
#define KERNEL_INVALID_ADDRESS    0x100  /* Address that is never mapped. */

#define USER_STACK_SIZE 4

DEFINE_ITERATOR_CTX(page_frame, PF_ITER_ARCH,
                    e820memmap_t *mmap;
                    uint32_t e820id;
                    );

static inline bool is_kernel_addr(void *a)
{
  uintptr_t addr = (uintptr_t)a;

  if (addr >= KERNEL_START_PHYS)
    return (addr <= KERNEL_END_PHYS);

  return ((k2p(addr) >= (uintptr_t)(BOOT_OFFSET - AP_BOOT_OFFSET)) &&
          (addr < KERNEL_START_PHYS));
}

static inline uintptr_t __allocate_vregion(ulong_t npages)
{
  __kernel_va_base -= (npages << PAGE_WIDTH);
  return __kernel_va_base;
}

static inline uintptr_t __reserve_uspace_vregion(ulong_t npages)
{
  __uspace_top_vaddr += (npages << PAGE_WIDTH);
  return __uspace_top_vaddr;
}

extern uintptr_t __utrampoline_virt;

void arch_mm_init(void);
void arch_mm_remap_pages(void);
void arch_smp_mm_init(cpu_id_t cpu);
void arch_mm_stage0_init(cpu_id_t cpu);
extern void __userspace_trampoline_codepage(void);
/* FIXME DK: remove after debugging */
struct __vmm;
void map_kernel_area(struct __vmm *vmm);

#endif /* __ARCH_MM_H__ */

