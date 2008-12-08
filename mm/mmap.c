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
#include <mlibc/kprintf.h>
#include <mlibc/stddef.h>
#include <mm/mm.h>
#include <mm/page.h>
#include <mm/mmap.h>
#include <mm/slab.h>
#include <mm/pfalloc.h>
#include <eza/mutex.h>
#include <eza/errno.h>
#include <eza/arch/page.h>
#include <eza/arch/mm.h>
#include <eza/arch/ptable.h>
#include <eza/arch/types.h>

#define DEFAULT_MAPDIR_FLAGS (MAP_RW | MAP_EXEC | MAP_USER)

root_pagedir_t kernel_root_pagedir;
static memcache_t *__rpd_memcache = NULL;

static inline pde_idx_t __number_of_entries(uintptr_t va_diff, pde_idx_t start_idx, pdir_level_t level)
{
  if (va_diff >=  pgt_get_range(level))
    return (PTABLE_DIR_ENTRIES - start_idx);
  else
    return (pgt_vaddr2idx(va_diff, level) + 1);
}

static inline void __check_entries_range(page_frame_t *dir, pde_t *pde)
{
  ASSERT(pagedir_get_level(dir) == PTABLE_LEVEL_FIRST);
  ASSERT(pgt_pde2idx(pde) < PTABLE_DIR_ENTRIES);
  ASSERT(pagedir_get_entries(dir) <= PTABLE_DIR_ENTRIES);
}

static status_t __check_va(root_pagedir_t *root_dir, uintptr_t va, /* OUT */uintptr_t *entry_addr)
{
  pdir_level_t level;
  page_frame_t *cur_dir = root_dir->dir;
  pde_t *pde;

  for (level = PTABLE_LEVEL_LAST; level > PTABLE_LEVEL_FIRST; level--) {
    pde = pgt_fetch_entry(cur_dir, pgt_vaddr2idx(va, level));    
    if (!pgt_pde_is_mapped(pde)) {
      if (entry_addr)
        *entry_addr = (uintptr_t)pde;
      
      return -EADDRNOTAVAIL;
    }

    cur_dir = pgt_pde_fetch_subdir(pde);
  }

  pde = pgt_fetch_entry(cur_dir, pgt_vaddr2idx(va, PTABLE_LEVEL_FIRST));
  if (entry_addr)
    *entry_addr = (uintptr_t)pde;
  if (!pgt_pde_is_mapped(pde))
    return -EADDRNOTAVAIL;

  return 0;
}

root_pagedir_t *root_pagedir_allocate(void)
{
  if (unlikely(!__rpd_memcache)) {
    __rpd_memcache = create_memcache("Root pagedirs cache", sizeof(root_pagedir_t),
                                     1, SMCF_PGEN | SMCF_GENERIC);
    if (!__rpd_memcache)
      panic("root_pagedir_allocate: Can't allocate memory cache for root page directories structures!");
  }
  return alloc_from_memcache(__rpd_memcache);
}

void root_pagedir_initialize(root_pagedir_t *rpd)
{
  rpd->dir = pagedir_create(PTABLE_LEVEL_LAST);
  if (!rpd->dir)
    panic("root_pagedir_initialize: Can't create root page director: ENOMEM");

  mutex_initialize(&rpd->lock);
  rpd->pin_type = 0;
  atomic_set(&rpd->refcount, 0);
}

void pin_root_pagedir(root_pagedir_t *rpd, int how)
{
  switch (how) {
      case RPD_PIN_RW:
        mutex_lock(&rpd->lock);        
        break;
      case RPD_PIN_RDONLY:
        mutex_lock(&rpd->lock);
        break;
      default:
        panic("pin_root_pagedir: Unknown type of lock operation: %d", how);
  }

  rpd->pin_type = how;
}

void unpin_root_pagedir(root_pagedir_t *rpd)
{
  if (!rpd->pin_type) {
    kprintf(KO_WARNING "Attemtion to unping already unpinned root page directory %p.\n", rpd);
    return;
  }
  else {
    int pin_type = rpd->pin_type;

    rpd->pin_type = 0;
    switch (pin_type) {
        case RPD_PIN_RDONLY:
          mutex_unlock(&rpd->lock);
          break;
        case RPD_PIN_RW:
          mutex_unlock(&rpd->lock);
          break;
        default:
          panic("unpin_root_pagedir: Unknown pin type: %d", pin_type);
    }
  }
}

status_t mm_check_va_range(root_pagedir_t *root_dir, uintptr_t va_from,
                           uintptr_t va_to, /* OUT */uintptr_t *entry_addr)
{
  uintptr_t va;
  status_t ret = 0;

  pin_root_pagedir(root_dir, RPD_PIN_RDONLY);
  for (va = PAGE_ALIGN_DOWN(va_from); va < va_to; va += PAGE_SIZE) {
    ret = __check_va(root_dir, va, entry_addr);
    if (ret)
      goto out;
  }

  out:
  unpin_root_pagedir(root_dir);
  return ret;
}

status_t mm_check_va(root_pagedir_t *root_dir, uintptr_t va, /* OUT */uintptr_t *entry_addr)
{
  status_t ret;
  
  pin_root_pagedir(root_dir, RPD_PIN_RDONLY);
  ret = __check_va(root_dir, va, entry_addr);
  unpin_root_pagedir(root_dir);

  return ret;
}

status_t pagedir_populate(pde_t *parent_pde, pde_flags_t flags)
{
  page_frame_t *subdir, *parent_dir;

  if (pgt_pde_is_mapped(parent_pde))
    return -EALREADY;

  parent_dir = pgt_pde2pagedir(parent_pde)
  ASSERT((pagedir_get_level(parent_dir) > PTABLE_LEVEL_FIRST) &&
         (pagedir_get_level(parent_dir) <= PTABLE_LEVEL_LAST));
  subdir = pagedir_create(pagedir_get_level(parent_dir) - 1);
  if (!subdir)
    return -ENOMEM;
 
  pgt_pde_save(parent_pde, pframe_number(subdir), flags);
  pagedir_set_entries(parent_dir, pagedir_get_entries(parent_dir) + 1);  
  ASSERT(parent_dir->entries <= PTABLE_DIR_ENTRIES);
  
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

  return 0;
}

status_t pagedir_map_entries(pde_t *pde_start, pde_idx_t entries,
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
    atomic_inc(&page->refcount);
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

status_t mmap(root_pagedir_t *root_dir, uintptr_t va, page_idx_t first_page, int npages, mmap_flags_t flags)
{  
  page_idx_t idx;
  mmapper_t mapper;
  status_t ret;
  page_frame_iterator_t pfi;
  ITERATOR_CTX(page_frame, PF_ITER_INDEX) pfi_index_ctx;
  
  mapper.va_from = PAGE_ALIGN(va);
  idx = virt_to_pframe_id((void *)mapper.va_from);
  mapper.va_to = mapper.va_from + ((npages - 1) << PAGE_WIDTH);
  mapper.flags = flags;
  mm_init_pfiter_index(&pfi, &pfi_index_ctx, first_page, first_page + npages - 1);
  mapper.pfi = &pfi;

  pin_root_pagedir(root_dir, RPD_PIN_RW);
  ret = __mmap_pages(root_dir->dir, &mapper, PTABLE_LEVEL_LAST);
  unpin_root_pagedir(root_dir);
  
  return ret;
}

int __mmap_pages(page_frame_t *dir, mmapper_t *mapper, pdir_level_t level)
{
  pde_idx_t idx, entries;
  int ret = 0;
  uintptr_t va_from_saved = mapper->va_from;
  pde_t *pde;
  
  idx = pgt_vaddr2idx(mapper->va_from, level);
  entries = __number_of_entries(mapper->va_to - mapper->va_from, idx, level);
  if (level == PTABLE_LEVEL_FIRST) {
    ret = pagedir_map_entries(pgt_fetch_entry(dir, idx), entries, mapper->pfi, mmap_flags2ptable_flags(mapper->flags));
    if (!ret)
      mapper->va_from += (pgt_get_range(level) - pgt_idx2vaddr(idx, level));
  }
  else {
    pde_flags_t _flags = mmap_flags2ptable_flags(DEFAULT_MAPDIR_FLAGS);
    
    do {
      pde = pgt_fetch_entry(dir, idx++);

      if (pgt_pde_is_mapped(pde)) {
        ret = -EALREADY;
        pde = NULL;
        goto unmap;
      }

      ret = pagedir_populate(pde, _flags);
      if (ret) {
        pde = NULL;
        goto unmap;
      }
      
      ret = __mmap_pages(pgt_pde_fetch_subdir(pde), mapper, level - 1);
      if (ret)
        goto unmap;
      
    } while (--entries > 0);
  }

  return ret;
  unmap:
  kprintf("ret = %d\n", ret);
  for (;;);
  if (pde) {
    if (!pagedir_get_entries(pgt_pde2pagedir(pde)))
      pagedir_depopulate(pde);
  }
  if (level == PTABLE_LEVEL_LAST)
    __unmap_pages(dir, va_from_saved, mapper->va_from, PTABLE_LEVEL_LAST);
  
  return ret;
}

void __unmap_pages(page_frame_t *dir, uintptr_t va_from, uintptr_t va_to, pdir_level_t level)
{
  pde_idx_t idx, entries;
  status_t ret = 0;

  idx = pgt_vaddr2idx(va_from, level);
  entries = __number_of_entries(va_to - va_from, idx, level);  
  if (level == PTABLE_LEVEL_FIRST)
    pagedir_unmap_entries(pgt_pagedir2pde(dir), entries);
  else {
    while (entries--) {
      pde_t *pde = pgt_fetch_entry(dir, idx);

      if (!pgt_pde_is_mapped(pde)) {
        ret = -EALREADY;
        goto panic;
      }
      
      __unmap_pages(pgt_pde_fetch_subdir(pde), va_from + pgt_get_range(level) - pgt_idx2vaddr(idx, level),
                    va_to, level - 1);
      idx++;
      if (!pagedir_get_entries(pgt_pde2pagedir(pde))) {
        ret = pagedir_depopulate(pde);
        if (ret)
          goto panic;
      }
    }
  }

  panic:
  panic("__unmap: Can't unmap virtual region: %p - %p. ERROR = %d\n", va_from, va_to, ret);
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
#endif /* CONFIG_DEBUG_MM */
