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
 * eza/arch/amd64/mm/ptable.c - AMD64-specific page table functionality.
 *
 */

#include <mlibc/assert.h>
#include <ds/iterator.h>
#include <mm/mm.h>
#include <mm/page.h>
#include <mm/pfalloc.h>
#include <mm/mmap.h>
#include <mlibc/types.h>
#include <eza/arch/ptable.h>

static inline page_frame_t *__create_pagedir(void)
{
  page_frame_t *pf = alloc_page(PFA_ZERO | PFA_PGEN);

  if (!pf)
    return NULL;

  pin_page_frame(pf);
  return pf;
}

static inline void __destroy_pagedir(page_frame_t *dir)
{
  unpin_page_frame(dir);
}

#define DEFAULT_PDIR_FLAGS (PDE_RW | PDE_US)

/*
 * Recursive function for mapping some address range.
 * Here we invent a term "pde_level". Pde level is a level of directory(PML4E, PDPE or PDE).
 * We go down and populate page directories until pde_level becomes equal to the level of
 * the lowest directory(PT) and after that we map the lowest level page table entries.
 */
static status_t do_ptable_map(page_frame_t *dir, mmap_info_t *minfo, int pde_level)
{
  int pde_idx, num_entries;
  uintptr_t addrs_range = pde_get_va_range(pde_level);
  status_t ret = 0;

  pde_idx = vaddr2pde_idx(minfo->va_from, pde_level);
  
  /* At this point we can clearly determine number of entries we have to map at level "pde_level" */
  if ((minfo->va_to - minfo->va_from) >= addrs_range)
    num_entries = PTABLE_DIR_ENTRIES - pde_idx;
  else
    num_entries = vaddr2pde_idx(minfo->va_to - minfo->va_from, pde_level);
  if (pde_level == PTABLE_LEVEL_FIRST) {
    /* we are standing at the lowest level page directory, so we can map given pages to page table from here. */
    ptable_map_entries(pde_fetch(dir, pde_idx), num_entries, minfo->pfi, minfo->flags, minfo->is_phys);
    minfo->va_from += addrs_range - pde_idx2vaddr(pde_idx, pde_level);
    return 0;
  }
  do { /* browse by all pdes at given level... */
    pde_t *pde = pde_fetch(dir, idx++);
    
    if (!(pde->flags & PDE_PRESENT)) {
      ret = ptable_populate_pagedir(pde, DEFAULT_PDIR_FLAGS);
      if (ret)
        return ret;
    }

    /* and go at 1 level down */
    dp_ptable_map(pde_fetch_subdir(pde), minfo, pde_level - 1);
  } while (--entries > 0);

  return ret;
}

/*
 * Unmap approach is very similar to do_ptable_map's one. do_ptable_unmap recursively
 * unmaps all pages in range from va_from to va_to. Decision how exactly pdes should
 * be unmapped is made depending on pde_level.
 */
static void do_ptable_unmap(page_frame_t *dir, uintptr_t va_from, uintptr_t va_to, int pde_level)
{
  pde_idx_t pde_idx, num_entries;
  status_t ret = 0;
  uintptr_t addrs_range = pde_get_va_range(pde_level);

  pde_idx = vaddr2pde_idx(va_from, level);
  if ((va_to - va_from) >= addrs_range)
    num_entries = PTABLE_DIR_ENTRIES - pde_idx;
  else
    num_entries = vaddr2pde_idx(va_to - va_from, pde_level);
  if (pde_level == PTABLE_LEVEL_FIRST) /* unmap pages in PT */
    ptable_unmap_entries(pde_fetch(dir, pde_idx), num_entries);
  else {
    while (num_entries--) {      
      pde_t *pde = pde_fetch(dir, pde_idx);

      if (!(pde->flags & PDE_PRESENT))
        panic("do_ptable_unmap: PDE %p level %d is already unmapped.", pde, pde_level);

      /* At first go down in a given PDE, then try to depopulate page directory. */
      do_ptable_unmap(pde_fetch_subdir(pde), va_from + addrs_range - pde_idx2vaddr(pde_idx, pde_level),
                      va_to, pde_level - 1);
      idx++;
      ptable_depopulate_pagedir(pde);
    }
  }
}

inline uint_t mpf2ptf(uint_t mmap_prot, uintptr_t mmap_flags)
{
  uint_t ptable_flags = 0;

  ptable_flags |= (!!(mmap_prot & PROT_WRITE) << bitnumber(PDE_RW));
  ptable_flags |= (!(mmap_prot & PROT_NONE) << bitnumber(PDE_US));
  ptable_flags |= (!!(mmap_prot & PROT_NOCACHE) << bitnumber(PDE_PCD));
  ptable_flags |= (!(mmap_prot & PROT_EXEC) << bitnumber(PDE_NX));
  ptable_flags |= (!!(mmap_flags & MAP_PHYS) << bitnumber(PDE_PHYS));
  
  return ptable_flags;
}

status_t ptable_rpd_initialize(rpd_t *rpd)
{
  rpd->pml4 = __create_pagedir();
  if (!prd->pml4)
    return -ENOMEM;
}

void ptable_rpd_deinitialize(rpd_t *rpd)
{
  __destroy_pagedir(rpd);
}

void ptable_rpd_clone(rpd_t *clone, rpd_t *src)
{
  clone->pml4 = src->pml4;
  pin_page_frame(link->clone);
}

void ptable_map_entries(pde_t *pde_start, int num_entries,
                        page_frame_iterator_t *pfi, uint_t flags)
{
  page_frame_t *dir = virt_to_pframe(pde_start), *page;
  pde_t *pde = pde_start;
  status_t ret = 0;

  ASSERT(num_entries > 0);
  while (num_entries--) {
    if (pde->flags & PDE_PRESENT)
      panic("ptable_map_entries: PDE %p is already mapped!", pde);

    iter_next(pfi);
    ASSERT(iter_isrunning(pfi));
    pde_set_flags(pde, flags | PDE_PRESENT);
    pgt_pde_save(pde++, pfi->pf_idx, flags);
    if (flags & PDE_PHYS)
      pin_page_frame(pframe_by_number(pfi->pf_idx));
  }
}

void ptable_unmap_entries(pde_t *pde_start, int num_entries)
{
  page_frame_t *dir = pgt_pde2pagedir(pde_start);
  pde_t *pde;

  ASSERT(num_entries > 0);
  for (pde = pde_start; num_entries--; pde++) {
    ASSERT(pde->flags & PDE_PRESENT);
    pde->flags &= ~PDE_PRESENT;
    if (pde_get_flags(pde) & PDE_PHYS)
      unpin_page_frame(pframe_by_number(pde_fetch_page_idx(pde)));
  }
}

status_t ptable_populate_pagedir(pde_t *parent_pde, uint_t flags)
{
  page_frame_t *subdir, *parent_dir;

  ASSERT(!(parent_pde->flags & PDE_PRESENT));
  parent_dir = virt_to_pframe(parent_pde);
  subdir = __create_pagedir();
  if (!subdir)
    return -ENOMEM;

  pde_set_page_idx(parent_pde, pframe_number(subdir));
  pde_set_flags(parent_pde, flags | PDE_PRESENT);  
  return 0;
}

void ptable_depopulate_pagedir(pde_t *dir)
{
  ASSERT(dir->flags & PDE_PRESENT);
  pde->flags &= ~PDE_PRESENT;
  __destroy_pagedir(pde);
}

status_t ptable_mmap(rpd_t *rpd, mmap_info_t *minfo)
{  
  if (minfo->va_to < minfo->va_from)
    return -EINVAL;
  if ((minfo->va_from != PAGE_ALIGN(minfo->va_from)) ||
      (minfo->va_to != PAGE_ALIGN(minfo->va_to)))
    return -EINVAL;
  
  return do_ptable_map(rpd->pml4, minfo, PTABLE_LEVEL_LAST);
}

status_t ptable_unmap(rpd_t *rpd, uintptr_t va_from, int npages)
{
  if ((npages < 0) || (va_from != PAGE_ALIGN(va_from)))
    return -EINVAL;

  do_ptable_unmap(rpd->pml4, va_from, va_from + (napges << PAGE_WIDTH), PTABLE_LEVEL_LAST);
  return 0;
}

#ifdef CONFIG_DEBUG_MM
static status_t __verify_mapping(root_pagedir_t *rpd, mmapper_t *mapper, /* OUT */mapping_dbg_info_t *minfo)
{
  uintptr_t va;
  status_t ret = 0;

  for (va = PAGE_ALIGN_DOWN(mapper->va_from); va < mapper->va_to; va += PAGE_SIZE) {
    ret = __check_va(rpd, va, &minfo->address);
    if (ret)
      return ret;

    iter_next(mapper->pfi);
    ASSERT(iter_isrunning(mapper->pfi));
    minfo->idx = pgt_pde_page_idx((pde_t *)minfo->address);
    if (minfo->idx != mapper->pfi->pf_idx) {
      minfo->expected_idx = mapper->pfi->pf_idx;
      minfo->address = va;
      return -EFAULT;
    }
  }

  return ret;
}

status_t __mm_verify_mapping(root_pagedir_t *rpd, mmapper_t *mapper, /* OUT */mapping_dbg_info_t *minfo)
{
  status_t ret;
  
  pin_root_pagedir(rpd, RPD_PIN_RDONLY);
  ret = __verify_mapping(rpd, mapper, minfo);
  unpin_root_pagedir(rpd);

  return ret;
}

status_t mm_verify_mapping(root_pagedir_t *rpd, uintptr_t va, page_idx_t first_page,
                           int npages, /* OUT */mapping_dbg_info_t *minfo)
{
  page_frame_iterator_t pfi;
  ITERATOR_CTX(page_frame, PF_ITER_INDEX) pfi_index_ctx;
  status_t ret;
  mmapper_t mapper;

  mapper.va_from = PAGE_ALIGN_DOWN(va);
  mapper.va_to = mapper.va_from + ((npages - 1) << PAGE_WIDTH);
  mm_init_pfiter_index(&pfi, &pfi_index_ctx, first_page, first_page + npages - 1);
  mapper.pfi = &pfi;

  pin_root_pagedir(rpd, RPD_PIN_RDONLY);
  ret = __verify_mapping(rpd, &mapper, minfo);
  unpin_root_pagedir(rpd);

  return ret;
}
