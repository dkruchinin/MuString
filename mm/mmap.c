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
 * mm/mmap.c - Architecture independend memory mapping API
 *
 */

#include <ds/iterator.h>
#include <ds/list.h>
#include <mlibc/stddef.h>
#include <mm/mm.h>
#include <mm/page.h>
#include <mm/mmap.h>
#include <mm/pfalloc.h>
#include <eza/spinlock.h>
#include <eza/errno.h>
#include <eza/arch/page.h>
#include <eza/arch/ptable.h>
#include <eza/arch/types.h>

#define DEFAULT_MAPDIR_FLAGS (MAP_RW | MAP_EXEC | MAP_USER)

page_frame_t *kernel_root_pagedir = NULL;
static RW_SPINLOCK_DEFINE(tmp_lock);

static inline pde_idx_t __number_of_entries(uitptr_t va_diff, pde_idx_t start_idx, pdir_level_t level)
{
  if (va_diff >=  pgt_get_range(level))
    return (PTABLE_DIR_ENTRIES - start_idx);
  else
    return (pgt_vaddr2idx(va_diff, level) + 1);
}

static inline void __check_entires_range(page_frame_t *dir, pde_t *pde)
{
  ASSERT(pagedir_get_level(dir) == PTABLE_LEVEL_FIRST);
  ASSERT(pgt_pde2idx(pde) < PTABLE_DIR_ENTRIES);
  ASSERT(pagedir_get_entries(dir) <= PTABLE_DIR_ENTRIES);
}

page_idx_t mm_pin_virt_addr(page_frame_t *dir, uintptr_t va)
{
  pdir_level_t level;
  page_frame_t *cur_dir = dir;
  pde_t *pde;

  for (level = PTABLE_LEVEL_LAST; level > PTABLE_LEVEL_FIRST; level--) {
    pde = pgt_fetch_entry(cur_dir, pgt_vaddr2idx(va, level));    
    if (!pgt_pde_is_mapped(pde))
      return -1;

    cur_dir = pgt_get_pde_subdir(pde);
  }

  pde = pgt_fetch_entry(cur_dir, pgt_vaddr2idx(va, PTABLE_LEVEL_FIRST));
  return (pgt_pde_is_mapped(pde) ? pgt_pde_page_idx(pde) : -1);
}

status_t pagedir_populate(pde_t *parent_pde, pde_flags_t flags)
{
  page_frame_t *subdir, *parent_dir = pgt_pde2pagedir(pde);

  if (pgt_pde_is_mapped(pde))
    return -EALREADY;

  ASSERT((pagedir_get_level(dir) > PTABLE_LEVEL_FIRST) &&
         (pagedir_get_level(dir) <= PTABLE_LEVEL_LAST));
  subdir = pagedir_create(pagedir_get_level(dir) - 1);
  if (!subdir)
    return -ENOMEM;
 
  pgt_pde_save(pde, pframe_number(subdir), flags);
  pagedir_set_entries(dir, pagedir_get_entries(parent_dir) + 1);  
  ASSERT(dir->entries <= PTABLE_DIR_ENTRIES);
  return 0;
}

status_t pagedir_depopulate(pde_t *pde)
{
  page_frame_t *dir = pgt_pde2pagedir(pde);
  
  if (!pgt_pde_is_mapped(pde))
    return -EALREADY;

  ASSERT((pagedir_get_level(dir) >= PTABLE_LEVEL_FIRST) &&
         (pagedir_get_level(dir) <= PTABLE_LEVEL_LAST));
  pgt_pde_delete(pde);
  if (!pagedir_get_entries(dir))
    pagedir_free(dir);
}

status_t pagedir_map_entris(pde_t *pde_start, pde_idx_t entries,
                            page_frame_iterator_t *pfi, pde_flags_t flags)
{
  page_frame_t *dir = pgt_pde2pagedir(pde_start), *page;
  pde_t *pde = pde_start;
  status_t ret = 0;

  __check_entries_range(dir, pde);
  
  while (entries--) {
    /*
     * Since we don't have any iformation about all mapped areas in kernel space
     * the only way to detect that some page or set of pages is already mapped is
     * simply trying to map them. If any error occurs during mapping process we have to unmap all
     * pages in range we tried to map.
     */
    if (pgt_pde_is_mapped(pde)) {
      kprintf(KO_ERROR "pagedir_map_entries: [%s] attempt to map already mapped entry %p second time.\n",
              pgt_level_name(pagedir_get_level(dir)), pde);
      ret = -EALREADY;
      goto error;
    }

    iter_next(pfi);
    ASSERT(iter_isrunning(pfi));
    pgt_pde_save(pde++, pfi->pf_idx, flags);
    page = pframe_by_number(pfi->pf_idx);
    atomic_inc(&pframe->refcount);
  }

  return 0;
  
  error:
  if (pde != pde_start) {
    while (pde >= pde_start)
      pgt_pde_delete(pde--);
  }

  return ret;
}

void pagedir_unmap_entries(pde_t *pde_start, pde_idx_t entries)
{
  page_frame_t *dir = pgt_pde2pagedir(pde_start), *page;
  pde_t *pde;

  __check_entries_range(dir, pde);
  for (pde = pde_start; entries--; pde++) {
    pgt_pde_delete(pde);
    page = pframe_by_number(pgt_pde_page_idx(pde));
    atomic_dec(&page->refcount);
  }
}

int mmap(page_frame_t *root_dir, uintptr_t va, page_idx_t first_page, int npages, mmap_flags_t flags)
{  
  page_idx_t idx;
  mmapper_t mapper;
  int ret;
  page_frame_iterator_t pfi;
  ITERATOR_CTX(page_frame, PF_ITER_INDEX) pfi_index_ctx;

  idx = virt_to_pframe_id((void *)va);
  mapper.va_from = va;
  mapper.va_to = va + ((npages - 1) << PAGE_WIDTH);
  mapper.flags = flags;
  mm_init_pfiter_index(&pfi, &pfi_index_ctx, first_page, first_page + npages - 1);
  mapper.pfi = &pfi;

  /* TODO: DK replace this ugly spinlock to counter or binary semaphore (RW?) */
  spinlock_lock_write(&tmp_lock);
  ret = mmap_pages(root_dir, &mapper);
  spinlock_unlock_write(&tmp_lock);
  return ret;
}

int __mmap_pages(page_frame_t *dir, mmapper_t *mapper, pdir_level_t level)
{
  pde_idx_t idx, entries, idx_start;
  int ret = 0;
  uintptr_t va_from_saved = mapper->va_from;
  
  idx_start = idx = pgt_vaddr2idx(mapper->va_from, level);
  entries = __number_of_entries(mapper->va_to - mapper->va_from, level);
  if (level == PTABLE_LEVEL_FIRST) {
    ret = mm_map_entries(pgt_fetch_entry(dir, idx), entries, mapper->pfi, pgt_translate_flags(mapper->flags));
    if (!ret)
      mapper->va_from += (pgt_get_range(level) - pgt_idx2vaddr(idx, level));
  }
  else {
    pde_flags_t _flags = pgt_translate_flags(DEFAULT_MAPDIR_FLAGS);
    
    do {
      pde_t *pde = pgt_fetch_entry(dir, idx++);
      
      if (!pgt_pde_is_mapped(pde)) {
        ret = mm_populate_pagedir(pde, _flags);
        if (ret < 0)
          goto unmap;
      }
      
      ret = __mmap_pages(pgt_get_pde_subdir(pde), mapper, level - 1);
      if (ret)
        goto unmap;
      
    } while (--entries > 0);
  }

  return ret;
  unmap:
  if (idx_start != idx) {
    while (idx >= idx_from) {
      pde_t *pde = pgt_fetch_entry(dir, idx--);
      
      if (!pgt_pde_is_mapped(pde))
        continue;
      
    }
  }
}

status_t __unmap(page_frame_t *dir, uintptr_t va_from, uitptr_t va_to, pdir_level_t level)
{
  pde_idx_t idx, entries;
  status_t ret = 0;

  idx = pgt_vaddr2idx(va_from, level);
  entries = __number_of_entries(va_to - va_from, level);  
  if (level == PTABLE_LEVEL_FIRST) {
    ret = mm_unmap_entries(dir, entries);
    goto panic;
  }
  else {
    while (entries--) {
      pde_t *pde = pgt_fetch_entry(dir, idx);

      if (!pgt_pde_is_mapped(pde))
        continue;

      ret = __unmap(pgt_pde_fetch_subdir(pde), va_from + pgt_get_range(level) - pgt_idx2vaddr(idx++, level),
                    va_to, level - 1);
      if (ret == -EALREADY)
        goto unmap;
      if (!pagedir_get_entries(pgt_get_pde_dir(pde)))
        mm_depopulate_pagedir(pde);
    }
  }

  return 0;
  panic:
  panic("__unmap: Can't unmap virtual region: %p - %p. ERROR = %d\n", va_from, va_to, ret);
}
