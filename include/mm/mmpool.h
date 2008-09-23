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

typedef uint8_t mm_pool_type_t;

#define POOL_DMA     __pool_type(PF_PDMA)
#define POOL_GENERAL __pool_type(PF_PGP)
#define POOL_CONT    __pool_type(PF_PCONT)
#define POOL_PERCPU  __pool_type(PF_PPERCPU)
#define __POOL_FIRST POOL_DMA

typedef struct __mm_pool {
  page_idx_t total_pages;
  page_idx_t reserved_pages;
  atomic_t free_pages;
  list_head_t reserved;
  page_frame_t *pages;
  pf_allocator_t allocator;
  mm_pool_type_t type;
  bool is_active;  
} mm_pool_t;

extern mm_pool_t mm_pools[NOF_MM_POOLS];

#define for_each_mm_pool(p)                                 \
  for (p = mm_pools; p < (mm_pools + NOF_MM_POOLS); p++)

#define for_each_active_mm_pool(p)              \
  for_each_mm_pool(p)                           \
    if((p)->is_active)

#define __pool_alloc_pages(pool, n)               \
  ((pool)->allocator.alloc_pages(n, (pool)->allocator.alloc_ctx))
#define __pool_free_pages(pool, pages)                                  \
  ((pool)->allocator.free_pages(pages, (pool)->allocator.alloc_ctx))

static inline mm_pool_t *mmpools_get_pool(mm_pool_type_t type)
{
  ASSERT((type >= __POOL_FIRST) && (type < NOF_MM_POOLS));
  return (mm_pools + type);
}

static inline char *mmpools_get_pool_name(mm_pool_type_t type)
{
  switch (type) {
      case POOL_DMA:
        return "DMA";
      case POOL_GENERAL:
        return "GENERAL";
      case POOL_CONT:
        return "CONTINUOS";
      case POOL_PERCPU:
        return "PERCPU";
      default:
        break;
  }

  return "UNKNOWN";
}

void mmpools_init(void);
void mmpools_add_page(page_frame_t *page);
void mmpools_init_pool_allocator(mm_pool_t *pool);
void mmpools_check_pool_pages(mm_pool_t *pool);

#endif /* __MMPOOL_H__ */
