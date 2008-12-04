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
#include <eza/arch/mm.h>
#include <eza/arch/ptable.h>
#include <eza/arch/types.h>

typedef page_frame_t * page_directory_t;

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

extern page_directory_t kernel_root_pagedir;
extern bool map_verbose;

typedef struct __mmapper {
  page_frame_iterator_t *pfi;
  uintptr_t va_from;
  uintptr_t va_to;
  mmap_flags_t flags;  
} mmapper_t;

#define mmap_pages(root_dir, minfo)             \
  __mmap_pages(root_dir, minfo, PTABLE_LEVEL_LAST)
#define mmap_kern_pages(minfo)                  \
  mmap_pages(kernel_root_pagedir, minfo)
#define mmap_kern(va, first_page, npages, flags)            \
  mmap(kernel_root_pagedir, va, first_page, npages, flags)
#define mm_virt_addr_is_mapped(root_dir, va)       \
  (mm_pin_virt_addr(root_dir, (uintptr_t)(va)) >= 0)

/*
 * low level mapping functions
 */
#define pagedir_get_level(pagedir)              \
  ((*(pagedir))->level)
#define pagedir_set_level(pagedir, new_level)       \
  ((*(pagedir))->level = (new_level))
#define pagedir_get_entries(pagedir)            \
  (*(pagedir)->entries)
#define pagedir_set_entries(pagedir, newval)    \
  (*(pagedir)->entries = (newval))
#define mm_init_root_pagedir(pagedir)           \
  mm_pagedir_initialize(pagedir, PTABLE_LEVEL_LAST)

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
status_t pagedir_unmap_entries(pde_t *pde_start, pde_idx_t entries);

int __mmap_pages(page_directory_t *dir, mmapper_t *mapper, pdir_level_t level);
int mmap(page_directory_t *root_dir, uintptr_t va, page_idx_t first_page, int npages, mmap_flags_t flags);
int mm_populate_pagedir(pde_t *pde, pde_flags_t flags);
int mm_map_entries(pde_t *pde_start, pde_idx_t entries,
                   page_frame_iterator_t *pfi, pde_flags_t flags);
page_idx_t mm_pin_virt_addr(page_directory_t *dir, uintptr_t va);

#endif /* __MMAP_H__ */
