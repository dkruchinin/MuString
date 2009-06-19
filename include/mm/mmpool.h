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
 */

#ifndef __MSTRING_MMPOOL_H__
#define __MSTRING_MMPOOL_H__

#include <config.h>
#include <arch/atomic.h>
#include <arch/mmpool_cofig.h>
#include <ds/list.h>
#include <mm/page.h>
#include <mm/page_alloc.h>
#include <sync/spinlock.h>
#include <mstring/kprintf.h>
#include <mstring/assert.h>
#include <mstring/panic.h>
#include <mstring/types.h>

#ifdef ARCH_NUM_MMPOOLS

#if (ARCH_NUM_MMPOOLS > MMPOOLS_MAX)
#error "Arch-specific number of memory pools exeeds MMPOOLS_MAX limit!"
#elif (ARCH_NUM_MMPOOLS <= 0)
#error "Arch-specific number of memory pools is <= 0!"
#endif /* RCH_NUM_MMPOOLS > MMPOOLS_MAX */

#else /* ARCH_NUM_MMPOOLS */

#error "ARCH_NUM_MMPOOLS is not defined!"
#endif /* !defined ARCH_NUM_MMPOOLS */

typedef uint8_t mmpool_flags_t;
typedef uint8_t mmpool_type_t;

#define MMPOOL_KERN   0x01
#define MMPOOL_USER   0x02
#define MMPOOL_DMA    0x04

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
typedef struct mmpool {
  char *name;                     /**< Memory pool name */
  page_allocator_t allocator;     /**< Page frames allocator attached to given pool */
  page_idx_t first_pidx;          /**< Memory pool's very first page frame number */
  page_idx_t num_pages;           /**< Total number of pages in the pool */
  page_idx_t num_reserved_pages;  /**< Number of reserved pages fitting given pool */
  atomic_t num_free_pages;        /**< Number of free in the pool */
  mmpool_flags_t flags;           /**< Memory pool flags */
  mmpool_type_t type;             /**< Unique integer fitting in 4 bits identifying memory pool type */
} mmpool_t;

extern mmpool_t *mmpools[ARCH_NUM_MMPOOLS];

/**
 * @def for_each_mm_pool(p)
 * @brief Iterate through all memory pools
 * @param[out] p - A pointer to mm_pool_t which will be used as iterator element
 *
 * @see mm_pool_t
 */
#define for_each_mmpool(p)                                 \
  for ((p) = mmpools; (p); (p)++)

/**
 * @brief Get pool by its type
 * @param type - pool type
 * @return A pointer to mm pool of type @a type
 */
static inline mmpool_t *get_mmpool_by_type(uint8_t type)
{
  if (unlikely(type >= MMPOOLS_MAX)) {
    panic("Attemption to get memory pool by unknown type %d!\n", type);
  }
  
  return mmpools[type];
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

INITCODE mmpool_type_t mmpool_register(mmpool_t *mmpool);
void mmpool_add_page(mmpool_type_t pool_type, page_frame_t *pframe);

#endif /* __MSTRING_MMPOOL_H__ */
