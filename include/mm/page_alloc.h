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
 * (c) Copyright 2008 Dan Kruchinin <dk@jarios.org>
 *
 */

#ifndef __MSTRING_PAGE_ALLOC_H__
#define __MSTRING_PAGE_ALLOC_H__

#include <mm/page.h>
#include <mstring/types.h>

/* Allocation flags */
#define AF_ZERO       (1 << MMPOOLS_SHIFT)
#define AF_CONTIG     (2 << MMPOOLS_SHIFT)
#define AF_ATOMIC     (4 << MMPOOLS_SHIFT)

#define PAFLAGS_MMPOOL_TYPE(flags) ((flags) & MMPOOLS_MASK)
#define PFRAME_MMPOOL_TYPE(page) PAFLAGS_MMPOOL_TYPE((page)->flags)

/**
 * @typedef uint8_t pfalloc_flags_t
 * Page frame allocation flags
 */
typedef uint32_t palloc_flags_t;

struct mmpool;

/**
 * @struct pf_allocator_t
 * Page frame allocator abstract type
 */
typedef struct page_allocator {
  void (*initialize)(struct mmpool *pool);
  page_frame_t *(*alloc_pages)(page_idx_t num_pages, void *data);
  void (*free_pages)(page_frame_t *pframe, page_idx_t num_pages, void *data);
  void (*dump)(void *data);
  char *name;
  page_idx_t min_block_size;
  page_idx_t max_block_size;
} page_allocator_t;

extern page_allocator_t *default_allocator;

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
page_frame_t *alloc_pages(page_idx_t num_pages, palloc_flags_t flags);

/**
 * @brief free continous block of pages starting from @a pages
 * @param pages - A pointer to the first page in freeing block
 * @note Internal page frames allocator *must* be able to determine block size by
 *       the very first page frame in block.
 */
void free_pages(page_frame_t *pages, page_idx_t num_pages);
void free_pages_chain(page_frame_t *pages);
uintptr_t sys_alloc_dma_pages(int num_pages);
void sys_free_dma_pages(uintptr_t paddr, int num_pages);

static inline void *alloc_pages_addr(int n, palloc_flags_t flags)
{
  page_frame_t *pf = alloc_pages(n, flags);
  if(pf)
    return pframe_to_virt(pf);

  return NULL;
}

static inline void free_pages_addr(void *addr, page_idx_t npages)
{
  page_frame_t *pf = virt_to_pframe(addr);
  free_pages(pf, npages);
}

#endif /* __MSTRING_PAGE_ALLOC_H__ */

