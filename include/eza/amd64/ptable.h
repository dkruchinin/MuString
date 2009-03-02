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
 * (c) Copyright 2008 Dan Kruchinin <dan.kruchinin@gmail.com>
 *
 * include/eza/amd64/ptable.h - AMD64-specific page-table management API
 *
 */

#ifndef __ARCH_PTABLE_H__
#define __ARCH_PTABLE_H__

#include <mm/page.h>
#include <eza/spinlock.h>
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

/* default flags for all page table directories */
#define PTABLE_DEF_PDIR_FLAGS (PDE_RW | PDE_US)

/**
 * @struct rpd_t
 * @brief Root page directory structures (AMD64-specific)
 */
typedef struct __rpd {
  page_frame_t *pml4;
  spinlock_t rpd_lock;
} rpd_t;

#define RPD_PAGEDIR(rpd) ((rpd)->pml4)

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

#define PTABLE_DIR_ENTRIES 0x200 /**< Number of entries per page table directory */
#define PTABLE_LEVEL_FIRST 0     /**< Level of the lowest pde */
#define PTABLE_LEVEL_LAST  3     /**< Level of the highest pde */

#define pde_is_present(pde) (!!((pde)->flags & PDE_PRESENT))
#define pde_is_kmapped(pde) (!((pde)->flags & PDE_US))

/**
 * @brief Translate virtual address to PDE index.
 * @param offset    - Virtual address
 * @param pde_level - A level of pde.
 * @return An index of PDE according to @a pde_level
 */
static inline pde_idx_t pde_offset2idx(uintptr_t offset, int pde_level)
{
  return (pde_idx_t)((offset >> (PAGE_WIDTH + 9 * pde_level)) & 0x1FF);
}

/**
 * @brief Translate PDE index to corresponding virtual address
 * @param pde_idx   - An index of PDE.
 * @param pde_level - A level of PDE.
 * @return Virtual address of PDE according to @a pde_level.
 * @see vaddr2pde_idx
 */
static inline uintptr_t pde_idx2offset(pde_idx_t pde_idx, int pde_level)
{
  return (((uintptr_t)pde_idx & 0x1FF) << (PAGE_WIDTH + 9 * pde_level));
}

/**
 * @brief Set PDE flags
 * @param pde   - A pointer to pde
 * @param flags - Flags that will be set
 */
static inline void pde_set_flags(pde_t *pde, ptable_flags_t flags)
{
  pde->flags = flags & 0x1FF;
  pde->nx = flags >> 13;
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

static inline void pde_save(pde_t *pde, page_idx_t page_idx, ptable_flags_t flags)
{
  pde_set_page_idx(pde, page_idx);
  pde_set_flags(pde, flags | PDE_PRESENT);
}

#define pde_set_not_present(pde) ((pde)->flags &= ~PDE_PRESENT)

/**
 * @brief Get PDE flags
 * @param pde - A pointer to pde flags will be readed from
 * @return PDE flags
 */
static inline ptable_flags_t pde_get_flags(pde_t *pde)
{
  return (pde->flags | (pde->nx << 13));
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
static inline pde_t *pde_fetch(page_frame_t *dir, pde_idx_t eidx)
{
  return ((pde_t *)pframe_to_virt(dir) + eidx);
}


/**
 * @brief Get a virtual addresses range given PDE level can hold.
 * @param pde_level - A level of PDE.
 * @return A range PDE at level @a pde_level can hold.
 */
static inline uintptr_t pde_get_addrs_range(int pde_level)
{
  return (((uintptr_t)PTABLE_DIR_ENTRIES << PAGE_WIDTH) << (9 * pde_level));
}

/**
 * @brief Translate kernel map flags to relevant page table flags
 * @param kmap_flags - Kerenel map flags
 * @return Page table flags
 */
ptable_flags_t kmap_to_ptable_flags(uint32_t kmap_flags);
uint32_t ptable_to_kmap_flags(ptable_flags_t flags);

#endif /* __ARCH_PTABLE_H__ */
