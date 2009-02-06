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
#include <eza/arch/page.h>
#include <eza/arch/types.h>

/* page-table related flags */
#define PDT_AMD64_PML4 3
#define PDT_AMD64_PDP  2
#define PDT_AMD64_PD   1
#define PDT_AMD64_PT   0

#define PTABLE_LEVEL_FIRST PDT_AMD64_PT
#define PTABLE_LEVEL_LAST  PDT_AMD64_PML4

typedef uint16_t pde_flags_t;

#define PDE_PRESENT  0x001
#define PDE_RW       0x002
#define PDE_US       0x004
#define PDE_PWT      0x008
#define PDE_PCD      0x010
#define PDE_AVAIL    0x020
#define PDE_DIRTY    0x040
#define PDE_PAT      0x080
#define PDE_GLOBAL   0x100
#define PDE_NX       0x200

typedef struct __pde {
  unsigned flags      :12;
  unsigned base_0_19  :20;
  unsigned base_20_39 :20;
  unsigned avail      :11;
  unsigned nx          :1;
} __attribute__((packed)) pde_t;

#define PTABLE_DIR_ENTRIES 0x200
#define PTABLE_DIR_MASK    0x1FF

#define pgt_pde_flags(pde) ((pde)->flags | ((pde)->nx << 9))
#define pgt_pde_page_idx(pde) ((pde)->base_0_19 | ((pde)->base_20_39 << 20))

#define pgt_pde_is_mapped(pde) ((pde)->flags & PDE_PRESENT)
#define pgt_get_pde_dir(pde) (virt_to_pframe((void *)(pde)))
#define pgt_get_pde_subdir(pde) (pframe_by_number(pgt_pde_page_idx(pde)))

#define pgt_pde_set_flags(pde, pde_flags)       \
  do {                                          \
    (pde)->flags = (pde_flags) & 0x1FF;         \
    (pde)->nx = (pde_flags) >> 9;               \
  } while (0)

#define pgt_pde_set_page_idx(pde, page_idx)     \
  do {                                          \
    (pde)->base_0_19 = (page_idx);              \
    (pde)->base_20_39 = (page_idx) >> 20;       \
  } while (0)

static inline pde_t *pgt_fetch_entry(page_frame_t *dir, page_idx_t eidx)
{
  return ((pde_t *)pframe_to_virt(dir) + eidx);
}

static inline void pgt_pde_save(pde_t *pde, page_idx_t page_idx, pde_flags_t flags)
{
  pgt_pde_set_flags(pde, flags | PDE_PRESENT);
  pgt_pde_set_page_idx(pde, page_idx);
}

static inline pde_idx_t pgt_vaddr2idx(uintptr_t vaddr, pdir_level_t level)
{
  return (pde_idx_t)((vaddr >> (PAGE_WIDTH + 9 * level)) & PTABLE_DIR_MASK);
}

static inline uintptr_t pgt_idx2vaddr(pde_idx_t idx, pdir_level_t level)
{
  return (((uintptr_t)idx & PTABLE_DIR_MASK) << (PAGE_WIDTH + 9 * level));
}

static inline uintptr_t pgt_get_range(pdir_level_t level)
{
  return ((uintptr_t)PTABLE_DIR_ENTRIES << PAGE_WIDTH) << (9 * level);
}

page_frame_t *pgt_create_pagedir(page_frame_t *parent, pdir_level_t level);
pde_flags_t pgt_translate_flags(unsigned int mmap_flags);

#endif /* __ARCH_PTABLE_H__ */
