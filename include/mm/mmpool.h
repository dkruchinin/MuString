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

#define GENERAL_POOL_TYPE 0
#ifdef CONFIG_DMA_POOL
#define DMA_POOL_TYPE     1
#else
#define DMA_POOL_TYPE GENERAL_POOL_TYPE
#endif /* CONFIG_DMA_POOL */

#define POOL_GENERAL() (&mm_pools[GENERAL_POOL_TYPE])
#define POOL_DMA()     (&mm_pools[DMA_POOL_TYPE])

#define MMPOOLS_NAME_LEN 32

#define MMP_IMMORTAL   0x01
#define MMP_ACTIVE     0x02
#define MMP_REGISTERED 0x04
#define MMPOOL_FLAGS_MASK (MMP_IMMORTAL | MMP_ACTIVE)

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
  char name[MMPOOL_NAME_LEN];   /**< Memory pool name */
  pf_allocator_t allocator;     /**< Pool's pages allocator */
  list_node_t pool_node;
  struct {
    page_idx_t total_pages;     /**< Total number of pages in pool */  
    page_idx_t reserved_pages;  /**< Number of reserved pages */
    atomic_t free_pages;        /**< Number of free pages (atomic) */
  } stat;
  rw_spinlock_t rwlock;
  uint8_t flags;
  uint8_t type;                 /**< Pool type */
} mm_pool_t;

extern mm_pool_t *mm_pools[CONFIG_NOF_MMPOOLS]; /**< An array of all pools */

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
    if(((p)->flags & MMP_ACTIVE))

#define mmpool_alloc_pages(pool, n)                                 \
  ((pool)->allocator.alloc_pages(n, (pool)->allocator.alloc_ctx))
#define mmpool_free_pages(pool, pages, numpgs)                          \
  ((pool)->allocator.free_pages(pages, numpgs, (pool)->allocator.alloc_ctx))
#define mmpool_pblock_size(pool, pblock)        \
  ((pool)->allocator.pages_block_size(pblock, (pool)->allocator.alloc_ctx))
#define mmpool_block_size_max(pool)             \
  ((pool)->allocator.block_sz_max)
#define mmpool_block_size_min(pool)             \
  ((pool)->allocator.block_sz_min)

/**
 * @brief Get pool by its type
 * @param type - pool type
 * @return A pointer to mm pool of type @a type
 *
 * @see mm_pool_type_t
 */
static inline mm_pool_t *mmpools_get_pool(uint8_t type)
{
  if (unlikely(type >= CONFIG_NOF_MMPOOLS))
    return NULL;
  
  return mm_pools[type];
}

mm_pool_t *mmpool_create(const char *name, uint8_t type, uint8_t flags);
int mmpool_register(mm_pool_t *pool, const char *name, uint8_t type, uint8_t flags);
int mmpool_add_page(mm_pool_t *pool, page_frame_t *page);

#endif /* __MMPOOL_H__ */
