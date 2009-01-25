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
 * include/eza/amd64/ptable.h - AMD64-specific page-table management API
 *
 */

#ifndef __ARCH_PTABLE_H__
#define __ARCH_PTABLE_H__

#include <ds/iterator.h>
#include <mm/page.h>
#include <eza/arch/types.h>

typedef enum __ptable_flags {
  PDE_PRESENT = 0x0001, /**< PDE is present. I.e. corresponding page content is holding in main memory */
  PDE_RW      = 0x0002, /**< PDE is accessible for read and write operations */
  PDE_US      = 0x0004, /**< User/suptervisor flag. If set, PDE is accessible from user-space. */
  PDE_PWT     = 0x0008, /**< Page-Level Writethrough. When bit is set,
                           page has writeback caching policy and writethrough otherwise */
  PDE_PCD     = 0x0010, /**< Page-Level Cache Disable. When bit is set, page is not cacheable. */
  PDE_ACC     = 0x0020, /**< This bit indicates whether the page-translation table or
                          physical page to which this entry points has been accessed */
  PDE_DIRTY   = 0x0040, /**< Indicates whether the page-translation table or physical page to which this
                           entry points has been written. */
  PDE_GLOBAL  = 0x0100, /**< The TLB entry for a global page (G=1) is not invalidated when CR3 is loaded */
  PDE_PAT     = 0x1000, /**< Page-Attribute Table. See “Page-Attribute Table Mechanism” on page 193
                           of AMD architecture programmers manual vol. 2 for more info. */
  PDE_NX      = 0x2000, /**< When the NX bit is set to 1, code cannot be executed from the mapped
                           physical pages. See “No Execute (NX) Bit” on page 143 of AMD architecture programmers
                           manual vol. 2 for more info. */
} ptable_flags_t;

typedef uint32_t pde_idx_t;

/**
 * @struct rpd_t
 * @brief Root page directory structures (AMD64-specific)
 */
typedef struct __rpd {
  page_frame_t *pml4;
} rpd_t;

/**
 * @struct pde_t
 * @brief AMD64-specific Page Directory Entry.
 *
 * PDE may be an entry of the lowest level of page table
 * as well as an entry of any higher level directory(i.e. a subdirectory)
 * Depending on what pde_t instance exactly is(lowest level entry or subdirectory),
 * its flags are managed.
 */
typedef struct __pde {
  unsigned flags      :12;
  unsigned base_0_19  :20;
  unsigned base_20_39 :20;
  unsigned avail      :11;
  unsigned nx          :1;
} __attribute__((packed)) pde_t;

DEFINE_ITERATOR_CTX(page_frame, PF_ITER_PTABLE,
                    rpd_t *rpd;
                    uintptr_t va_cur;
                    uintptr_t va_from;                    
                    uintptr_t va_to;
                    );

#define PTABLE_DIR_ENTRIES 0x200 /**< Number of entries per page table directory */
#define PTABLE_LEVEL_FIRST 0     /**< Level of the lowest pde */
#define PTABLE_LEVEL_LAST  3     /**< Level of the highest pde */

/**
 * @brief Set PDE flags
 * @param pde   - A pointer to pde
 * @param flags - Flags that will be set
 */
static inline void pde_set_flags(pde_t *pde, uint_t flags)
{
  pde->flags = flags & 0x1FF;
  pde->nx = flags >> 13;
}

/**
 * @brief Get PDE flags
 * @param pde - A pointer to pde flags will be readed from
 * @return PDE flags
 */
static inline uint_t pde_get_flags(pde_t *pde)
{
  return (pde->flags | (pde->nx << 13));
}

/**
 * @brief Save page index to the PDE.
 * @param pde - A pointer tp pde index will be saved to
 * @param idx - Page index.
 */
static inline void pde_set_page_idx(pde_t *pde, page_idx_t idx)
{
  pde->base_0_19 = idx;
  pde->base_20_39 = idx >> 20;
}

/**
 * @brief Fetch page index from the PDE.
 * @param pde - A pointer to pde index will be fetched from.
 * @return Page index.
 */
static inline page_idx_t pde_fetch_page_idx(pde_t *pde)
{
  return ((pde_idx_t)pde->base_0_19 | ((pde_idx_t)pde->base_20_39 << 20));
}

/**
 * @brief Fetch subdirectory("subPDE") from a given pde
 * @param pde - A pointer to pde_t
 * @return A pointer to page frame that is a subdirectory of @a pde.
 */
static inline page_frame_t *pde_fetch_subdir(pde_t *pde)
{
  return pframe_by_number(pde_fetch_page_idx(pde));
}

/**
 * @brief Fetch PDE holding in a page directory by its index.
 * @param dir  - A pointer to corresponding to parent PDE page directory
 * @param eidx - Child PDE index in the page directory @a dir
 * @return A pointer to child pde
 */
static inline pde_t *pde_fetch(page_frame_t *dir, int eidx)
{
  return ((pde_t *)pframe_to_virt(dir) + eidx);
}

/**
 * @brief Translate virtual address to PDE index.
 * @param vaddr     - Virtual address
 * @param pde_level - A level of pde.
 * @return An index of PDE according to @a pde_level
 * @see pde_idx2vaddr
 */
static inline pde_idx_t vaddr2pde_idx(uintptr_t vaddr, int pde_level)
{
  return (pde_idx_t)((vaddr >> (PAGE_WIDTH + 9 * pde_level)) & 0x1FF);
}

/**
 * @brief Translate PDE index to corresponding virtual address
 * @param pde_idx   - An index of PDE.
 * @param pde_level - A level of PDE.
 * @return Virtual address of PDE according to @a pde_level.
 * @see vaddr2pde_idx
 */
static inline uintptr_t pde_idx2vaddr(int pde_idx, int pde_level)
{
  return (((uintptr_t)pde_idx & 0x1FF) << (PAGE_WIDTH + 9 * pde_level));
}

/**
 * @brief Get a virtual addresses range given PDE level can hold.
 * @param pde_level - A level of PDE.
 * @return A range PDE at level @a pde_level can hold.
 */
static inline uintptr_t pde_get_va_range(int pde_level)
{
  return (((uintptr_t)PTABLE_DIR_ENTRIES << PAGE_WIDTH) << (9 * pde_level));
}

/**
 * @brief Translate kernel map flags to relevant page table flags
 * @param kmap_flags - Kerenel map flags
 * @return Page table flags
 */
ptable_flags_t kmap_to_ptable_flags(ulong_t kmap_flags);

/**
 * @brief Initialize root page table directory (PML4)
 * @param rpd - A pointer to rpd_t
 * @return 0 on success, -ENOMEM if page allocation failed.
 * @see rpd_t
 */
int ptable_rpd_initialize(rpd_t *rpd);

/**
 * @brief Deinitialize root page table directory (PML4)
 *
 * Actually this function will free pml4's pages if
 * it is not linked from any other places.
 *
 * @param rpd - A pointer to rpd_t
 * @see rpd_t
 */
void ptable_rpd_deinitialize(rpd_t *rpd);

/**
 * @brief Clone some existing PML4
 * @param clone - A pointer to root page directory that will become a clone
 * @param src  -  A pointer to root page directory that will be cloned
 */
void ptable_rpd_clone(rpd_t *clone, rpd_t *src);

/**
 * @brief Map pages to the lowest level page direatory(PT)
 * @param pde_start   - A pointer to pde mapping starts from
 * @param num_entries - A number of entries to map
 * @param pfi         - A pointer to page frame iterator containing at least @a num_entries items
 * @param flags       - Page table flag
 * @see page_frame_iterator_t
 * @see pde_t
 */
uintptr_t ptable_map_entries(page_frame_t *parent_dir, uintptr_t va, int num_entries,
                             page_frame_iterator_t *pfi, uint_t flags);

/**
 * @brief Unmap pages from the lowest level page directory(PT)
 * @param pde_start   - A pointer to pde unmapping starts from
 * @param num_entries - A number of entries to unmap
 * @see pde_t
 */
void ptable_unmap_entries(pde_t *pde_start, int num_entries);

/**
 * @brief Populate page directory
 *
 * The following flags can not be used with page directories:
 * PDE_GLOBAL, PDE_PAT and PDE_PHYS
 *
 * @param parent_pde - A pointer to parent pde
 * @param pde_level  - Actual page directory level.
 * @param flags      - Page direcotory flags
 * @return 0 on success, -ENOMEM if page allocation failed.
 * @see pde_t
 */
int ptable_populate_pagedir(pde_t *parent_pde, int pde_level, ptable_flags_t flags);

/**
 * @brief Depopulate page direcotry
 * @param dir - A pointer to pde_t of directory to depopulate
 * @see pde_t
 */
void ptable_depopulate_pagedir(pde_t *dir);

/**
 * @brief Map pages into the given root page directory.
 * @param rpd     - A pointer to root page directory pages will be mapped to
 * @param va_from - Virtual address showing where mapping must start.
 * @param npages  - Number of pages to map.
 * @param pfi     - Page frame iterator containing @a npages items.
 * @param flags   - Page table flags
 *
 * @return 0 on succes, -ENOMEM if there is not enough memory.
 * @see rpd_t
 * @see page_table_flags
 */
int ptable_map(rpd_t *prd, uintptr_t va_from, ulong_t npages,
               page_frame_iterator_t *pfi, ptable_flags_t flags);

/**
 * @brief Unmap pages from the given root page directory
 * @param rpd     - A pointer to root page direcotory
 * @param va_from - Virtual address pages will be unmapped from
 * @param npages  - Number of pages to unmap.
 * @return 0 on success, -EINVAL on error.
 * @see rpd_t
 */
void ptable_unmap(rpd_t *rpd, uintptr_t va_from, page_idx_t npages);

page_idx_t mm_vaddr2page_idx(rpd_t *rpd, uintptr_t vaddr);

#endif /* __ARCH_PTABLE_H__ */
