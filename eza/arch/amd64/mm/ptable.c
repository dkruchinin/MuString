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
#include <mm/page.h>
#include <mm/pfi.h>
#include <mm/pfalloc.h>
#include <mm/mmap.h>
#include <mm/vmm.h>
#include <mm/mm.h>
#include <mlibc/types.h>
#include <eza/arch/ptable.h>
#include <eza/arch/mm.h>
#include <eza/arch/mm_types.h>

static inline page_frame_t *__create_pagedir(void)
{
  page_frame_t *pf = alloc_page(AF_ZERO | AF_PGEN);

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
    num_entries = vaddr2pde_idx(minfo->va_to - minfo->va_from, pde_level) + 1;

  if (pde_level == PTABLE_LEVEL_FIRST) {
    /* we are standing at the lowest level page directory, so we can map given pages to page table from here. */
    ptable_map_entries(pde_fetch(dir, pde_idx), num_entries, minfo->pfi, minfo->ptable_flags);
    minfo->va_from += addrs_range - pde_idx2vaddr(pde_idx, pde_level);
    return 0;
  }
  do { /* browse by all pdes at given level... */
    pde_t *pde = pde_fetch(dir, pde_idx++);
    
    if (!(pde->flags & PDE_PRESENT)) {
      ret = ptable_populate_pagedir(pde, DEFAULT_PDIR_FLAGS);
      if (ret)
        return ret;
    }

    /* and go at 1 level down */
    ret = do_ptable_map(pde_fetch_subdir(pde), minfo, pde_level - 1);
    if (ret)
      return ret;
  } while (--num_entries > 0);

  return 0;
}

/*
 * Unmap approach is very similar to do_ptable_map's one. do_ptable_unmap recursively
 * unmaps all pages in range from va_from to va_to. Decision how exactly pdes should
 * be unmapped is made depending on pde_level.
 */
static void do_ptable_unmap(page_frame_t *dir, uintptr_t va_from, uintptr_t va_to, int pde_level)
{
  pde_idx_t pde_idx, num_entries;
  uintptr_t addrs_range = pde_get_va_range(pde_level);

  pde_idx = vaddr2pde_idx(va_from, pde_level);
  if ((va_to - va_from) >= addrs_range)
    num_entries = PTABLE_DIR_ENTRIES - pde_idx;
  else
    num_entries = vaddr2pde_idx(va_to - va_from, pde_level) + 1;
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
      pde_idx++;
      ptable_depopulate_pagedir(pde);
    }
  }
}

inline uint_t mpf2ptf(uint_t mmap_prot, uint_t mmap_flags)
{
  uint_t ptable_flags = 0;

  ptable_flags |= (!!(mmap_prot & PROT_WRITE) << bitnumber(pow2(PDE_RW)));
  ptable_flags |= (!(mmap_prot & PROT_NONE) << bitnumber(pow2(PDE_US)));
  ptable_flags |= (!!(mmap_prot & PROT_NOCACHE) << bitnumber(pow2(PDE_PCD)));
  ptable_flags |= (!(mmap_prot & PROT_EXEC) << bitnumber(pow2(PDE_NX)));
  ptable_flags |= (!!(mmap_flags & MAP_PHYS) << bitnumber(pow2(PDE_PHYS)));
  
  return ptable_flags;
}

status_t ptable_rpd_initialize(rpd_t *rpd)
{
  rpd->pml4 = __create_pagedir();
  if (!rpd->pml4)
    return -ENOMEM;

  return 0;
}

void ptable_rpd_deinitialize(rpd_t *rpd)
{
  __destroy_pagedir(rpd->pml4);
}

void ptable_rpd_clone(rpd_t *clone, rpd_t *src)
{
  clone->pml4 = src->pml4;
  pin_page_frame(clone->pml4);
}

void ptable_map_entries(pde_t *pde_start, int num_entries,
                        page_frame_iterator_t *pfi, uint_t flags)
{
  pde_t *pde = pde_start;

  ASSERT(num_entries > 0);
  while (num_entries--) {
    if (pde->flags & PDE_PRESENT)
      panic("ptable_map_entries: PDE %p is already mapped!", pde);

    ASSERT(!iter_isstopped(pfi));
    iter_next(pfi);
    if (pfi->pf_idx == PAGE_IDX_INVAL)
      panic("ptable_map_entries: Unexpected page index. ERR = %d", pfi->error);
    pde_set_flags(pde, flags | PDE_PRESENT);
    pde_set_page_idx(pde++, pfi->pf_idx);
    if (flags & PDE_PHYS)
      pin_page_frame(pframe_by_number(pfi->pf_idx));
  }
}

void ptable_unmap_entries(pde_t *pde_start, int num_entries)
{
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
  dir->flags &= ~PDE_PRESENT;
  __destroy_pagedir(virt_to_pframe(dir));
}

status_t ptable_map(rpd_t *rpd, mmap_info_t *minfo)
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

  do_ptable_unmap(rpd->pml4, va_from, va_from + (npages << PAGE_WIDTH), PTABLE_LEVEL_LAST);
  return 0;
}

static void __pfi_first(page_frame_iterator_t *pfi);
static void __pfi_next(page_frame_iterator_t *pfi);

void pfi_ptable_init(page_frame_iterator_t *pfi,
                     ITERATOR_CTX(page_frame, PF_ITER_PTABLE) *ctx,
                     rpd_t *rpd, uintptr_t va_from, int npages)
{
  pfi->first = __pfi_first;
  pfi->next = __pfi_next;
  pfi->last = pfi->prev = NULL;
  iter_init(pfi, PF_ITER_PTABLE);
  memset(ctx, 0, sizeof(*ctx));
  ctx->va_from = ctx->va_cur = va_from;
  ctx->va_to = va_from + (npages << PAGE_WIDTH);
  ctx->rpd = rpd;  
  pfi->pf_idx = PAGE_IDX_INVAL;
  pfi->state = ITER_LIE;
  pfi->error = 0;
  iter_set_ctx(pfi, ctx);
}

static void __pfi_first(page_frame_iterator_t *pfi)
{
  ITERATOR_CTX(page_frame, PF_ITER_PTABLE) *ctx;
  
  ASSERT(pfi->type == PF_ITER_PTABLE);
  ctx = iter_fetch_ctx(pfi);
  pfi->pf_idx = mm_vaddr2page_idx(ctx->rpd, ctx->va_from);
  if (pfi->state == PAGE_IDX_INVAL) {
    pfi->error = -EFAULT;
    pfi->state = ITER_STOP;
    return;
  }
  else {
    pfi->error = 0;
    ctx->va_cur = ctx->va_from;
  }
  
  pfi->state = (ctx->va_cur < ctx->va_to) ?
    ITER_RUN : ITER_STOP;
}

static void __pfi_next(page_frame_iterator_t *pfi)
{
  ASSERT(pfi->type == PF_ITER_PTABLE);
  if (!iter_isrunning(pfi))
    iter_first(pfi);
  else {
    ITERATOR_CTX(page_frame, PF_ITER_PTABLE) *ctx;    
    ctx = iter_fetch_ctx(pfi);
    ctx->va_cur += PAGE_SIZE;
    pfi->pf_idx = mm_vaddr2page_idx(ctx->rpd, ctx->va_cur);
    if (pfi->pf_idx == PAGE_IDX_INVAL) {
      pfi->error = -EFAULT;
      pfi->state = ITER_STOP;
      return;
    }
    if (ctx->va_cur >= ctx->va_to)
      pfi->state = ITER_STOP;
  }
}
