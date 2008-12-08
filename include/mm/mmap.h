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
 * include/mm/mmap.h - Architecture independend memory mapping interface
 *
 */

#ifndef __MMAP_H__
#define __MMAP_H__

#include <mlibc/string.h>
#include <mm/page.h>
#include <eza/kernel.h>
#include <eza/mutex.h>
#include <eza/spinlock.h>
#include <eza/arch/mm.h>
#include <eza/arch/ptable.h>
#include <eza/arch/atomic.h>
#include <eza/arch/types.h>

/**
 * @typedef uint8_t mmap_flags_t
 * Memory mapping flags.
 */
typedef uint8_t mmap_flags_t;

/* flags for memory mapping */
#define MAP_READ      0x01 /**< Mapped page may be readed */
#define MAP_WRITE     0x02 /**< Mapped page may be written */
#define MAP_RW        0x03 /**< Mapped page may be both readed and written */
#define MAP_USER      0x04 /**< Mapped page is visible for user */
#define MAP_EXEC      0x08 /**< Mapped page may be executed */
#define MAP_DONTCACHE 0x10 /**< Prevent caching of mapped page */

typedef struct __mmapper {
  page_frame_iterator_t *pfi;
  uintptr_t va_from;
  uintptr_t va_to;
  mmap_flags_t flags;  
} mmapper_t;

typedef struct __root_pagedir {
  page_frame_t *dir;  
  mutex_t lock;
  atomic_t refcount;
  int pin_type;  
} root_pagedir_t;

enum {
  RPD_PIN_RDONLY = 1,
  RPD_PIN_RW,
};

extern root_pagedir_t kernel_root_pagedir;

root_pagedir_t *root_pagedir_allocate(void);
void root_pagedir_initialize(root_pagedir_t *rpd);
void pin_root_pagedir(root_pagedir_t *rpd, int how);
void unpin_root_pagedir(root_pagedir_t *rpd);

static inline root_pagedir_t *root_pagedir_mklink(root_pagedir_t *src)
{
  ASSERT(src->dir != NULL);
  atomic_inc(&src->refcount);  
  return src;
}

/*
 * low level mapping functions
 */
#define pagedir_get_level(pagedir)              \
  ((pagedir)->level)
#define pagedir_set_level(pagedir, new_level)       \
  ((pagedir)->level = (new_level))
#define pagedir_get_entries(pagedir)            \
  ((pagedir)->entries)
#define pagedir_set_entries(pagedir, newval)    \
  ((pagedir)->entries = (newval))

static inline page_frame_t *pagedir_create(pdir_level_t level)
{
  page_frame_t *dir = pgt_allocate_pagedir(level);

  if (!dir)
    return NULL;
  
  pagedir_set_level(dir, level);
  pagedir_set_entries(dir, 0);
  return dir;
}

static inline void pagedir_free(page_frame_t *dir)
{
  pagedir_set_level(dir, 0);
  pagedir_set_entries(dir, 0);
  pgt_free_pagedir(dir);
}

status_t pagedir_populate(pde_t *pde, pde_flags_t flags);
status_t pagedir_depopulate(pde_t *pde);
status_t pagedir_map_entries(pde_t *pde_start, pde_idx_t entries,
                             page_frame_iterator_t *pfi, pde_flags_t flags);
void pagedir_unmap_entries(pde_t *pde_start, pde_idx_t entries);

/* middle-level mapping functions */
status_t __mmap_pages(page_frame_t *dir, mmapper_t *mapper, pdir_level_t level);
void __unmap_pages(page_frame_t *dir, uintptr_t va_from, uintptr_t va_to, pdir_level_t level);

/* high-level mapping functions */
#define mmap_kern(va, first_page, npages, flags)            \
  mmap(&kernel_root_pagedir, va, first_page, npages, flags)
#define mmap_kern_pages(minfo)                  \
  mmap_pages(&kernel_root_pagedir, minfo)
#define unmap_kern(va, npages)                  \
  unmap(&kernel_root_pagedir, va, npages)

status_t mmap(root_pagedir_t *root_dir, uintptr_t va, page_idx_t first_page, int npages, mmap_flags_t flags);

static inline void unmap(root_pagedir_t *root_dir, uintptr_t va, int npages)
{
  uintptr_t _va = PAGE_ALIGN(va);
  
  pin_root_pagedir(root_dir, RPD_PIN_RW);
  __unmap_pages(root_dir->dir, _va, _va + (npages << PAGE_WIDTH), PTABLE_LEVEL_LAST);
  unpin_root_pagedir(root_dir);
}

static inline status_t mmap_pages(root_pagedir_t *root_dir, mmapper_t *minfo)
{
  status_t ret;
  
  minfo->va_from = PAGE_ALIGN(minfo->va_from);
  minfo->va_to = PAGE_ALIGN(minfo->va_to);
  pin_root_pagedir(root_dir, RPD_PIN_RW);
  ret = __mmap_pages(root_dir->dir, minfo, PTABLE_LEVEL_LAST);
  unpin_root_pagedir(root_dir);

  return ret;
}

/*
 * Mapping checking and validating functions
 */
#define mm_va_is_mapped(root_dir, va)           \
  !mm_check_va(root_dir, va, NULL)

status_t mm_check_va(root_pagedir_t *rood_dir, uintptr_t va, uintptr_t *entry_addr);
status_t mm_check_va_range(root_pagedir_t *root_dir, uintptr_t va_from,
                           uintptr_t va_to, uintptr_t *entry_addr);

static inline page_frame_t *mm_va_page(root_pagedir_t *root_dir, uintptr_t va)
{
  pde_t pde;

  if (mm_check_va(root_dir, va, (uintptr_t *)&pde))
    return NULL;

  return pframe_by_number(pgt_pde_page_idx(&pde));
}

#ifdef CONFIG_DEBUG_MM
typedef struct __mapping_dbg_info {
  uintptr_t address;
  page_idx_t idx;
  page_idx_t expected_idx;
} mapping_dbg_info_t;

status_t mm_verify_mapping(root_pagedir_t *rpd, uintptr_t va,
                           page_idx_t first_page, int npages, mapping_dbg_info_t *minfo);
status_t __mm_verify_mapping(root_pagedir_t *rpd, mmapper_t *mapper, mapping_dbg_info_t *minfo);
#endif /* CONFIG_DEBUG_MM */
#endif /* __MMAP_H__ */
