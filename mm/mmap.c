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
#include <mm/pfalloc.h>
#include <mm/vmm.h>
#include <eza/mutex.h>
#include <eza/errno.h>
#include <eza/arch/page.h>
#include <eza/arch/mm.h>
#include <eza/arch/ptable.h>
#include <eza/arch/types.h>

rpd_t kernel_rpd;

#if 0
#define DEFAULT_MAPDIR_FLAGS (MAP_READ | MAP_WRITE | MAP_EXEC | MAP_USER)

static memcache_t *__rpd_memcache = NULL;

static inline int __number_of_entries(uintptr_t va_diff, int start_pde_idx, uintptr_t range)
{
  if (va_diff >=  pgt_get_range(level))
    return (PTABLE_DIR_ENTRIES - start_idx);
  else
    return (pgt_vaddr2idx(va_diff, level) + 1);
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
#endif

status_t mmap_kern(uintptr_t va, page_idx_t first_page, int npages,
                   uint_t proto, uint_t flags)
{
  mmap_info_t minfo;
  page_frame_iterator_t pfi;
  ITERATOR_CTX(page_frame, PF_ITER_INDEX) pf_idx_ctx;

  minfo.va_from = PAGE_ALIGN_DOWN(va);
  minfo.va_to = minfo.va_from + ((npages - 1) << PAGE_WIDTH);
  minfo.ptable_flags = mpf2ptf(proto, flags);
  pfi_index_init(&pfi, &pf_idx_ctx, first_page, first_page + npages - 1);
  return ptable_map(&kernel_rpd, &minfo);
}
