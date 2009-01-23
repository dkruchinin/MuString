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
 * include/mm/mmpool.h: MM-pools
 *
 */

#ifndef __MMPOOL_H__
#define __MMPOOL_H__

#include <config.h>
#include <mlibc/assert.h>
#include <ds/list.h>
#include <mm/page.h>
#include <mm/pfalloc.h>
#include <eza/spinlock.h>
#include <eza/arch/atomic.h>
#include <eza/arch/types.h>

/**
 * @typedef uint8_t mm_pool_type_t
 * @brief Pool type
 * @see POOL_DMA
 * @see POOL_GENERAL
 */
typedef uint8_t mm_pool_type_t;

/**
 * DMA pool (may exists on x86 and x86_64) architectures
 * if IOMMU is not supported
 */
#define POOL_DMA __pool_type(PF_PDMA)

/**
 * General purpose memory pool.
 * Typically contains most of pages.
 */
#define POOL_GENERAL __pool_type(PF_PGEN)
#define __POOL_FIRST POOL_DMA

/**
 * @struct mm_pool_t
 * @brief Memory pool structure
 * Memory pool is kind of abstraction under physical memory.
 * In other words pool contains some range of physical pages. This
 * allows logically split physical memory. Pool may have an allocator
 * which allows to allocate pages from given pool.
 *
 * @see page_frame_t
 * @see atomic_t
 * @see pf_allocator_t
 * @see mm_pool_type_t
 */
typedef struct __mm_pool {
  page_idx_t total_pages;    /**< Total number of pages in pool */
  page_idx_t reserved_pages; /**< Number of reserved pages */
  atomic_t free_pages;       /**< Number of free pages (atomic) */
  list_head_t reserved;      /**< A list of all reserved pages in pool */
  page_frame_t *pages;       /**< First page pool owns (last page is pool->pages + pool->total_pages - 1) */
  pf_allocator_t allocator;  /**< Pool pages allocator */
  mm_pool_type_t type;       /**< Pool type */
  bool is_active;            /**< Determines if pool is active */
} mm_pool_t;

extern mm_pool_t mm_pools[NOF_MM_POOLS]; /**< An array of all pools */

/**
 * @def for_each_mm_pool(p)
 * @brief Iterate through all memory pools
 * @param[out] p - A pointer to mm_pool_t which will be used as iterator element
 *
 * @see mm_pool_t
 */
#define for_each_mm_pool(p)                                 \
  for (p = mm_pools; p < (mm_pools + NOF_MM_POOLS); p++)

/**
 * @def for_each_active_mm_pool(p)
 * @brief Iterate through each active memory pool
 * @param[out] p - A pointer to mm_pool_t which will be used as iterator element
 *
 * @see mm_pool_t
 */
#define for_each_active_mm_pool(p)              \
  for_each_mm_pool(p)                           \
    if((p)->is_active)

/* [internal] allocate n pages from pool */
#define __pool_alloc_pages(pool, n)               \
  ((pool)->allocator.alloc_pages(n, (pool)->allocator.alloc_ctx))

/* [internal] free pages to pool */
#define __pool_free_pages(pool, pages, numpgs)                          \
  ((pool)->allocator.free_pages(pages, numpgs, (pool)->allocator.alloc_ctx))

#define __pool_pblock_size(pool, pblock)        \
  ((pool)->allocator.pages_block_size(pblock, (pool)->allocator.alloc_ctx))

#define __pool_block_size_max(pool)             \
  ((pool)->allocator.block_sz_max)
#define __pool_block_size_min(pool)             \
  ((pool)->allocator.block_sz_min)

/**
 * @brief Get pool by its type
 * @param type - pool type
 * @return A pointer to mm pool of type @a type
 *
 * @see mm_pool_type_t
 */
static inline mm_pool_t *mmpools_get_pool(mm_pool_type_t type)
{
  ASSERT((type >= __POOL_FIRST) && (type < NOF_MM_POOLS));
  return (mm_pools + type);
}

/**
 * @brief Get pool name by its type
 * @param type - pool type
 * @return A name of pool with type @a type
 *
 * @see mm_pool_type_t
 */
static inline char *mmpools_get_pool_name(mm_pool_type_t type)
{
  switch (type) {
      case POOL_DMA:
        return "DMA";
      case POOL_GENERAL:
        return "GENERAL";
      default:
        break;
  }

  return "UNKNOWN";
}

/**
 * @brief Initialize memory pools
 * @note It's an initcall, so it must be called only once during boot stage.
 */
void mmpools_init(void);

/**
 * @brief Add page to related pool.
 * @a page should have pool type set in its flags.
 *
 * @param page - A pointer to existent page_frame_t
 * @see page_frame_t
 */
void mmpools_add_page(page_frame_t *page);

/**
 * @brief Initialize pool's memory allocator
 * @param pool - A pointer to alive memory pool.
 *
 * @see mm_pool_t
 */
void mmpools_init_pool_allocator(mm_pool_t *pool);

#endif /* __MMPOOL_H__ */
