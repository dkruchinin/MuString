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
 * mm/mmpool.c: MM-pools
 *
 */

#include <config.h>
#include <ds/list.h>
#include <mstring/string.h>
#include <mm/mmpool.h>
#include <mm/page.h>
#include <mm/tlsf.h>
#include <mstring/panic.h>
#include <arch/atomic.h>
#include <arch/types.h>

mm_pool_t mm_pools[MMPOOLS_MAX];

void mmpools_initialize(void)
{
  uint8_t i;
  
  kprintf("[MM] Initialize memory pools.\n");
  memset(mm_pools, 0, sizeof(*mm_pools) * MMPOOLS_MAX);
  for (i = 0; i < MMPOOLS_MAX; i++) {
    switch (i) {
        case GENERAL_POOL_TYPE:
          mm_pools[i].name = "General";
          break;
        case DMA_POOL_TYPE:
          mm_pools[i].name = "DMA";
          break;
        case HIGHMEM_POOL_TYPE:
          mm_pools[i].name = "Highmem";
          break;
        case BOOTMEM_POOL_TYPE:
          mm_pools[i].name = "Bootmem";
          break;
        default:
          panic("Unknown memory pool type: %d\n", i);
    }

    mm_pools[i].type = i;
    mm_pools[i].is_active = false;
    mm_pools[i].first_page_id = PAGE_IDX_INVAL;
  }
}

void mmpool_add_page(mm_pool_t *pool, page_frame_t *pframe)
{
  if (pframe_number(pframe) < pool->first_page_id)
    pool->first_page_id = pframe_number(pframe);

  pool->total_pages++;
  if (pframe->flags & PF_RESERVED)
    pool->reserved_pages++;
  else
    atomic_inc(&pool->free_pages);

  pframe->pool_type = pool->type;
}

void mmpool_activate(mm_pool_t *pool)
{
  ASSERT(pool->type < MMPOOLS_MAX);
  ASSERT(!pool->is_active);
  switch (pool->type) {
      case GENERAL_POOL_TYPE: case DMA_POOL_TYPE:
        tlsf_allocator_init(pool);
        tlsf_validate_dbg(pool->allocator.alloc_ctx);
        break;
      default:
        panic("Unknown memory pool type: %d!", pool->type);
  }

  pool->is_active = true;
}
