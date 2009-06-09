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
 * mstring/genarch/mm/ptable.c: General page table API implementation. Common for some
 * architectures.
 */

#include <config.h>
#include <ds/iterator.h>
#include <mm/page.h>
#include <mm/pfi.h>
#include <mstring/errno.h>
#include <mstring/genarch/ptable.h>
#include <arch/ptable.h>
#include <arch/tlb.h>
#include <mstring/types.h>

static inline void map_one_entry(rpd_t *rpd, pde_t *pde, page_idx_t pidx, ptable_flags_t flags)
{
  bool pde_was_present;
  page_frame_t *cur_dir = virt_to_pframe(pde);

  pde_was_present = pde_is_present(pde);
  pde_save(pde, pidx, flags);

  /*
   * If an entry already has been present in a given PDE it's very likely
   * it has been cached in the TLB. Thus we shuld explicitly flush it.
   */
  if (pde_was_present) {
    tlb_flush_entry(rpd, (uintptr_t)pde);
  }
  else {
    /* Increment number of entries in the first-level directory */
    pin_page_frame(cur_dir);
  }
}

static inline void unmap_one_entry(rpd_t *rpd, pde_t *pde)
{
  page_frame_t *cur_dir = virt_to_pframe(pde);

  pde_set_not_present(pde);
  tlb_flush_entry(rpd, (uintptr_t)pde);
  atomic_dec(&cur_dir->refcount);
}

static pde_idx_t __count_num_entries(pde_idx_t pde_idx, uintptr_t va_from, uintptr_t va_to, int pde_level)
{
  pde_idx_t num_entries;

  if ((va_to - (va_from -  pde_idx2offset(pde_idx, pde_level)))
      > pde_get_addrs_range(pde_level)) {
    num_entries = PTABLE_DIR_ENTRIES - pde_idx;
  }
  else
    num_entries = pde_offset2idx(va_to, pde_level) - pde_idx + 1;

  return num_entries;
}

static void unmap_entries(rpd_t *rpd, pde_t *start_pde, pde_idx_t num_entries, bool unpin_pages)
{
  pde_t *pde = start_pde;

  ASSERT((num_entries > 0) && (num_entries <= PTABLE_DIR_ENTRIES));
  while (num_entries--) {
    if (!pde_is_present(pde)) {
      pde++;
      continue;
    }

    unmap_one_entry(rpd, pde);
    if (unpin_pages && page_idx_is_present(pde_fetch_page_idx(pde)))
      unpin_page_frame(pframe_by_number(pde_fetch_page_idx(pde)));

    pde++;
  }
}

static void map_entries(rpd_t *rpd, pde_t *start_pde, pde_idx_t num_entries,
                        page_frame_iterator_t *pfi, ptable_flags_t flags, bool pin_pages)
{
  pde_t *pde = start_pde;

  ASSERT((num_entries > 0) && (num_entries <= PTABLE_DIR_ENTRIES));
  while (num_entries--) {
    ASSERT(!iter_isstopped(pfi));
    if (pfi->pf_idx == PAGE_IDX_INVAL)
      panic("Unexpected page index. ERR = %d(%d)", pfi->error, num_entries);

    map_one_entry(rpd, pde, pfi->pf_idx, flags);

    /*
     * Increment page refcount *only* if we're not mapping some kernel-only area
     * or if a mapped page is a valid visible from kernel physical frame.
     */
    if (pin_pages && page_idx_is_present(pfi->pf_idx))
      pin_page_frame(pframe_by_number(pfi->pf_idx));

    pde++;
    iter_next(pfi);
  }
}

static int populate_pagedir(pde_t *pde, ptable_flags_t flags)
{
  page_frame_t *subdir, *current_dir;

  ASSERT(!(pde->flags & PDE_PRESENT));
  current_dir = virt_to_pframe(pde);
  subdir = generic_create_pagedir();
  if (!subdir)
    return -ENOMEM;

  pde_save(pde, pframe_number(subdir), flags);
  pin_page_frame(current_dir);

  return 0;
}

static void depopulate_pagedir(pde_t *pde)
{
  page_frame_t *current_dir;

  ASSERT(pde_is_present(pde));
  current_dir = virt_to_pframe(pde);
  pde_set_not_present(pde);
  tlb_flush_entry(task_get_rpd(current_task()), (uintptr_t)pde);
  atomic_dec(&current_dir->refcount);
}

/*
 * Unmap approach is very similar to do_ptable_map's one. do_ptable_unmap recursively
 * unmaps all pages in range from va_from to va_to. Decision how exactly pdes should
 * be unmapped is made depending on pde_level.
 */
static uintptr_t do_ptable_unmap(rpd_t *rpd, page_frame_t *dir, uintptr_t va_from,
                                 uintptr_t va_to, int pde_level, bool unpin_pages)
{
  page_idx_t pde_idx, num_entries;

  pde_idx = pde_offset2idx(va_from, pde_level);
  num_entries = __count_num_entries(pde_idx, va_from, va_to, pde_level);
  if (pde_level == PTABLE_LEVEL_FIRST) /* unmap pages in PT */ {
    unmap_entries(rpd, pde_fetch(dir, pde_idx), num_entries, unpin_pages);
    return (num_entries << PAGE_WIDTH);
  }
  else {
    pde_t *pde = pde_fetch(dir, pde_idx);
    page_frame_t *subdir;

    while (num_entries--) {
      if (!pde_is_present(pde)) {
        pde++;
        continue;
      }

      /* At first go down in a given PDE, then try to depopulate page directory. */
      subdir = pde_fetch_subdir(pde);
      va_from += do_ptable_unmap(rpd, subdir, va_from , va_to, pde_level - 1, unpin_pages);
      if (!atomic_get(&subdir->refcount)) {
        free_page(subdir);
        depopulate_pagedir(pde);
      }

      pde++;
    }
  }

  return va_from;
}

struct pt_mmap_info {
  rpd_t *rpd;
  uintptr_t va_from;
  uintptr_t va_to;
  page_frame_iterator_t *pfi;
  ptable_flags_t flags;
  bool pin_pages;
};

/*
 * Recursive function for mapping given address range.
 * Here we invent a term "pde_level". Pde level is a level of directory(PML4E, PDPE or PDE).
 * We go down and populate page directories until pde_level becomes equal to the level of
 * the lowest directory(PT) and after that we map the lowest level page table entries.
 *
 * Another thing that should be kept in mind is so-called "page-table recovery".
 * There may be a crappy situation when mapping is partially completed(I mean one or more
 * pages were mapped in low level directories) and attemption to allocate new page directory
 * yields -ENOMEM. In this case all previousely mapped pages must be:
 *  1) unmapped
 *  2) there refcounts must be decremented
 */
static int do_ptable_map(page_frame_t *dir, struct pt_mmap_info *minfo, int pde_level)
{
  pde_idx_t pde_idx, num_entries;

  pde_idx = pde_offset2idx(minfo->va_from, pde_level);
  num_entries = __count_num_entries(pde_idx, minfo->va_from, minfo->va_to, pde_level);
  if (pde_level == PTABLE_LEVEL_FIRST) {
      /* we are standing at the lowest level page directory, so we can map given pages to page table from here. */
    map_entries(minfo->rpd, pde_fetch(dir, pde_idx), num_entries,
                minfo->pfi, minfo->flags, minfo->pin_pages);
    minfo->va_from += num_entries << PAGE_WIDTH;
    return 0;
  }
  else {
    pde_t *pde = pde_fetch(dir, pde_idx);
    int ret = 0;

    while (num_entries--) { /* browse through by all pdes at a given level... */
      if (!pde_is_present(pde)) {
        ret = populate_pagedir(pde, PTABLE_DEF_PDIR_FLAGS);
        if (ret) {
          return ret;
        }
      }

      /* and go at 1 level down */
      ret = do_ptable_map(pde_fetch_subdir(pde), minfo, pde_level - 1);
      if (ret)
        return ret;

      pde++;
    }
  }

  return 0;
}

page_frame_t *generic_create_pagedir(void)
{
  page_frame_t *pf = alloc_page(ptable_ops.alloc_flags);
  if (!pf)
    return NULL;

  return pf;
}

void generic_ptable_unmap(rpd_t *rpd, uintptr_t va_from, page_idx_t npages, bool unpin_pages)
{
  do_ptable_unmap(rpd, RPD_PAGEDIR(rpd), va_from,
                  va_from + ((npages - 1) << PAGE_WIDTH), PTABLE_LEVEL_LAST, unpin_pages);
  tlb_flush(rpd);
}

int generic_ptable_map(rpd_t *rpd, uintptr_t va_from, page_idx_t npages,
                       page_frame_iterator_t *pfi, ptable_flags_t flags, bool pin_pages)
{
  struct pt_mmap_info ptminfo;
  int ret;
  ITERATOR_CTX(page_frame, PF_ITER_INDEX) *ctx;

  ptminfo.va_from = va_from;
  ptminfo.va_to = va_from + ((npages - 1) << PAGE_WIDTH);
  ptminfo.pfi = pfi;
  ptminfo.flags = flags;
  ptminfo.pin_pages = pin_pages;
  ptminfo.rpd = rpd;

  ctx = iter_fetch_ctx(pfi);
  ret = do_ptable_map(RPD_PAGEDIR(rpd), &ptminfo, PTABLE_LEVEL_LAST);
  iter_next(pfi);

  if (ret) {
    kprintf_dbg("Failed to create mapping %p -> %p. Cleaning modified by the mapping entries:\n"
               "  [dirty entries]: %p -> %p\n", va_from, va_from + ((npages - 1) << PAGE_WIDTH),
                va_from, ptminfo.va_from);
  }
  else if (!iter_isstopped(pfi)) {
    kprintf_dbg("ptable_map: Got more pages than need for mapping of an area "
                "from %p to %p. (%d pages is enough)\n", va_from, ptminfo.va_to, npages);
  }

  tlb_flush(rpd);
  return ret;
}

page_idx_t generic_vaddr2page_idx(rpd_t *rpd, uintptr_t vaddr, /* OUT */ pde_t **retpde)
{
  uintptr_t va = PAGE_ALIGN_DOWN(vaddr);
  page_frame_t *cur_dir = RPD_PAGEDIR(rpd);
  pde_t *pde;
  int level;

  if (retpde)
    *retpde = NULL;
  for (level = PTABLE_LEVEL_LAST; level > PTABLE_LEVEL_FIRST; level--) {
    pde = pde_fetch(cur_dir, pde_offset2idx(va, level));
    if (!pde_is_present(pde))
      return PAGE_IDX_INVAL;

    cur_dir = pde_fetch_subdir(pde);
  }

  pde = pde_fetch(cur_dir, pde_offset2idx(va, PTABLE_LEVEL_FIRST));
  if (retpde)
    *retpde = pde;
  if (!pde_is_present(pde))
    return PAGE_IDX_INVAL;

  return pde_fetch_page_idx(pde);
}

int generic_map_page(rpd_t *rpd, uintptr_t addr, page_idx_t pidx, ptable_flags_t flags)
{
  page_frame_t *cur_dir = RPD_PAGEDIR(rpd);
  pde_t *pde;
  int level, ret = 0;

  ASSERT(!(addr & PAGE_MASK));
  for (level = PTABLE_LEVEL_LAST; level > PTABLE_LEVEL_FIRST; level--) {
    pde = pde_fetch(cur_dir, pde_offset2idx(addr, level));
    if (!pde_is_present(pde)) {
      ret = populate_pagedir(pde, PTABLE_DEF_PDIR_FLAGS);
      if (ret)
        return ret;
    }

    cur_dir = pde_fetch_subdir(pde);
  }

  pde = pde_fetch(cur_dir, pde_offset2idx(addr, PTABLE_LEVEL_FIRST));
  map_one_entry(rpd, pde, pidx, flags);
  tlb_flush_entry(rpd, addr);
  return 0;
}

void generic_unmap_page(rpd_t *rpd, uintptr_t addr)
{
  page_frame_t *cur_dir = RPD_PAGEDIR(rpd);
  int level;
  pde_t *pde, *dirspath[PTABLE_LEVEL_LAST];

  ASSERT(!(addr & PAGE_MASK));
  for (level = PTABLE_LEVEL_LAST; level > PTABLE_LEVEL_FIRST; level--) {
    pde = pde_fetch(cur_dir, pde_offset2idx(addr, level));
    if (!pde_is_present(pde))
      return;

    dirspath[level - 1] = pde;
    cur_dir = pde_fetch_subdir(pde);
  }

  pde = pde_fetch(cur_dir, pde_offset2idx(addr, PTABLE_LEVEL_FIRST));
  if (!pde_is_present(pde))
    return;

  unmap_one_entry(rpd, pde);
  tlb_flush_entry(rpd, addr);
  for (level = PTABLE_LEVEL_FIRST; level < PTABLE_LEVEL_LAST; level++) {
    cur_dir = pde_fetch_subdir(dirspath[level]);
    if (!atomic_get(&cur_dir->refcount)) {
      free_page(cur_dir);
      depopulate_pagedir(dirspath[level]);
    }
  }
}