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
 * include/mm/pfalloc.h: page frame allocation API
 *
 */

/**
 * @file include/mm/pfalloc.h
 * page frame allocation API
 * @author Dan Kruchinin
 */

#ifndef __PFALLOC_H__
#define __PFALLOC_H__ 

#include <mm/page.h>
#include <mlibc/types.h>

/* Allocation flags */
#define AF_MMP_BMEM  mmpool_type2flags()
#define AF_MMP_GEN   0x02
#define AF_MMP_DMA   0x04
#define AF_MMP_HMEM  0x08
#define AF_ZERO      0x10  /**< Allocate clean page block(fill block with zeros) */
#define AF_CLEAR_RC  0x20  /**< Allocate pages with refcounts set to 0 */
#define AF_USER      0x40

#define PAGES_POOL_MASK (AF_MMP_BMEM | AF_MMP_GEN | AF_MMP_DMA | AF_MMP_HMEM)

/**
 * @typedef uint8_t pfalloc_flags_t
 * Page frame allocation flags
 */
typedef uint32_t pfalloc_flags_t;

/**
 * @struct pfalloc_type_t
 * Contains types of all available allocator.
 * Each allocator has its own unique type.
 */
typedef enum __pfalloc_type {
    PFA_IDALLOC = 1, /**< Init-data(bootmem) allocator */
    PFA_TLSF,        /**< TLSF O(1) allocator */
} pfalloc_type_t;

/**
 * @struct pf_allocator_t
 * Page frame allocator abstract type
 */
typedef struct __pf_allocator {
  page_frame_t *(*alloc_pages)(page_idx_t n, void *data);
  page_frame_t *(*alloc_pages_max_avail)(void *data);
  void (*free_pages)(page_frame_t *pframe, page_idx_t num_pages, void *data);
  void (*dump)(void *data);
  void (*deactivate)(void *data);
  void *alloc_ctx;         /**< Internal allocator private data */
  pfalloc_type_t type;     /**< Allocator type */
} pf_allocator_t;

/**
 * @def alloc_page(flags)
 * @brief Allocate one page
 *
 * @param flags - Allocation flags
 * @return A pointer to page_frame_t on success, NULL on failure
 *
 * @see alloc_pages
 */
#define alloc_page(flags)                       \
  alloc_pages(1, flags)
#define free_page(page)                         \
  free_pages(page, 1)

/**
 * @brief Allocate @a n continous pages
 * @param n     - Number of continous pages to allocate
 * @param flags - Allocation flags
 * @return A pointer to first page_frame_t in block on success, NULL on failure
 *
 * @see alloc_page
 */
page_frame_t *alloc_pages(page_idx_t n, pfalloc_flags_t flags);

/**
 * @brief free continous block of pages starting from @a pages
 * @param pages - A pointer to the first page in freeing block
 * @note Internal page frames allocator *must* be able to determine block size by
 *       the very first page frame in block.
 */
void free_pages(page_frame_t *pages, page_idx_t num_pages);
page_idx_t pages_block_size(page_frame_t *first_page);

struct __vmm;

page_frame_t *alloc_pages_ncont(page_idx_t npages, pfalloc_flags_t flags);
void free_pages_ncont(page_frame_t *pages);

static inline void *alloc_pages_addr(int n, pfalloc_flags_t flags)
{
  page_frame_t *pf = alloc_pages(n, flags);
  if(pf)
    return pframe_to_virt(pf);

  return NULL;
}

static inline void free_pages_addr(void *addr)
{
  page_frame_t *pf = virt_to_pframe(addr);
  free_pages(pf, pages_block_size(pf));
}

#endif /* __PFALLOC_H__ */

