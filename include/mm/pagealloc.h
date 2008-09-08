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
 * (c) Copyright 2008 Michael Tsymbalyuk <mtzaurus@gmail.com>
 *
 * include/mm/pagealloc.h: Contains types and prototypes for kernel page
 *                         allocator.
 *
 */

#include <mm/mm.h>
#include <eza/arch/page.h>
#include <eza/arch/types.h>

#ifndef __PAGEALLOC_H__
#define __PAGEALLOC_H__ 

/* Main MM interface. */
page_frame_t *alloc_page( page_flags_t flags, int clean_page );

static inline void *pframe_to_virt(page_frame_t *frame)
{
  return (void *)(KERNEL_BASE + frame->idx * PAGE_SIZE);
}

static inline page_idx_t pframe_number(page_frame_t *frame)
{
  return frame->idx;
}

static inline void *pframe_phys_addr(page_frame_t *frame)
{
  return (void *)((uintptr_t)frame->idx * PAGE_SIZE);
}

static inline void *virt_to_phys(void *virt)
{
  return virt - KERNEL_BASE;
}

static inline void *pframe_id_to_virt( page_idx_t idx )
{
  return (void *)(KERNEL_BASE + idx * PAGE_SIZE);
}

static inline page_idx_t virt_to_pframe_id(void *virt)
{
  page_idx_t idx = ((uintptr_t)virt - KERNEL_BASE) >> PAGE_WIDTH;
  return idx;
}

/**
 * This function allocates one page using kernel memory allocator.
 *
 * @flags Memory allocation flags
 * @return Valid virtual address of a newly-allocated memory page
 * or NULL if allocation failed.
 */
static inline void *__alloc_page( page_flags_t flags, int clean_page ) {
  page_frame_t *pf = alloc_page(flags,clean_page);
  if( pf != NULL ) {
    return pframe_to_virt(pf);
  } else {
    return NULL;
  }
}

#endif

