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
#include <mstring/assert.h>
#include <ds/list.h>
#include <mm/page.h>
#include <mm/pfalloc.h>
#include <sync/spinlock.h>
#include <mstring/kprintf.h>
#include <arch/atomic.h>
#include <arch/types.h>

#define MMPOOLS_MAX   4

enum {
  GENERAL_POOL_TYPE = 0,  
  HIGHMEM_POOL_TYPE,
  DMA_POOL_TYPE,
  BOOTMEM_POOL_TYPE,
};
  
#define POOL_BOOTMEM() (&mm_pools[BOOTMEM_POOL_TYPE])
#define POOL_GENERAL() (&mm_pools[GENERAL_POOL_TYPE])
#define POOL_DMA()     (&mm_pools[DMA_POOL_TYPE])
#define POOL_HIGHMEM() (&mm_pools[HIGHMEM_POOL_TYPE])

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
  char *name;
  pf_allocator_t allocator;      /**< Pool's pages allocator */
  page_idx_t first_page_id;      /**< Number of very first page in a pool */
  page_idx_t total_pages;        /**< Total number of pages in pool */
  page_idx_t reserved_pages;     /**< Number of reserved pages */
  atomic_t free_pages;           /**< Number of free pages (atomic) */
  bool is_active;
  uint8_t type;
} mm_pool_t;

extern mm_pool_t mm_pools[MMPOOLS_MAX]; /**< An array of all pools */

/**
 * @def for_each_mm_pool(p)
 * @brief Iterate through all memory pools
 * @param[out] p - A pointer to mm_pool_t which will be used as iterator element
 *
 * @see mm_pool_t
 */
#define for_each_mm_pool(p)                                 \
  for (p = mm_pools; p < (mm_pools + MMPOOLS_MAX); p++)

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

/**
 * @brief Get pool by its type
 * @param type - pool type
 * @return A pointer to mm pool of type @a type
 *
 * @see mm_pool_type_t
 */
static inline mm_pool_t *get_mmpool_by_type(uint8_t type)
{
  if (unlikely(type >= MMPOOLS_MAX)) {
    kprintf(KO_WARNING "Attemption to get memory pool by unknown type %d!\n", type);
    return NULL;
  }
  
  return &mm_pools[type];
}

static inline page_frame_t *mmpool_alloc_pages(mm_pool_t *pool, page_idx_t num_pages)
{
  if (likely(pool->allocator.alloc_pages != NULL))
    return pool->allocator.alloc_pages(num_pages, pool->allocator.alloc_ctx);

  kprintf(KO_WARNING "Memory pool \"%s\" doesn't support alloc_pages function!\n", pool->name);
  return NULL;  
}

static inline void mmpool_free_pages(mm_pool_t *pool, page_frame_t *pages, page_idx_t num_pages)
{
  if (likely(pool->allocator.free_pages != NULL))
    return pool->allocator.free_pages(pages, num_pages, pool->allocator.alloc_ctx);

  kprintf(KO_WARNING "Memory pool \"%s\" doesn't support free_pages function!\n", pool->name);
}

static inline void mmpool_allocator_dump(mm_pool_t *pool)
{
  if (likely(pool->allocator.dump != NULL)) {
    pool->allocator.dump(pool->allocator.alloc_ctx);
    return;
  }

  kprintf(KO_WARNING "Memory pool \"%s\" doesn't support dump function!\n", pool->name);
}

void mmpools_initialize(void);
void mmpool_activate(mm_pool_t *pool);
void mmpool_add_page(mm_pool_t *pool, page_frame_t *pframe);

#endif /* __MMPOOL_H__ */
