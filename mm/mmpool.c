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
#include <mlibc/string.h>
#include <mm/mmpool.h>
#include <mm/page.h>
#include <mm/tlsf.h>
#include <mm/idalloc.h>
#include <eza/kernel.h>
#include <eza/arch/atomic.h>
#include <eza/arch/types.h>

#if (CONFIG_NOF_MMPOOLS > MMPOOLS_MAX)
#error "CONFIG_NOF_MMPOOLS can not be greater than MMPOOLS_MAX!"
#endif /* (CONFIG_NOF_MMPOOLS > MMPOOLS_MAX) */

mm_pool_t mm_pools[CONFIG_NOF_MMPOOLS];

void mmpools_initialize(void)
{
  uint8_t i;
  
  kprintf("[MM] Initialize memory pools.\n");
  memset(mm_pools, 0, sizeof(*mm_pools) * CONFIG_NOF_MMPOOLS);
  for (i = 0; i < CONFIG_NOF_MMPOOLS; i++) {
    switch (i) {
        case BOOTMEM_POOL_TYPE:
          mm_pools[i].name = "Bootmem";
          break;
        case GENERAL_POOL_TYPE:
          mm_pools[i].name = "General";
          break;
        case DMA_POOL_TYPE:
          mm_pools[i].name = "DMA";
          break;
        case HIGHMEM_POOL_TYPE:
          mm_pools[i].name = "Highmem";
          break;
        default:
          panic("Unknown memory pool type: %d\n", i);
    }

    mm_pools[i].type = i;
    mm_pools[i].is_active = false;
    mm_pools[i].first_page_idx = PAGE_IDX_INVAL;
  }
}

void mmpool_add_page(mm_pool_t *pool, page_frame_t *pframe)
{
  if (pframe_number(pframe) < pool->first_page_idx)
    pool->first_page_idx = pframe_number(pframe);

  pool->total_pages++;
  if (pframe->flags & PF_RESERVED)
    pool->reserved_pages++;
  else
    atomic_inc(&pool->free_pages);

  page->pool_type = pool->type;
}

void mmpool_activate(mm_pool_t *pool)
{
  ASSERT(pool->type < CONFIG_NOF_MMPOOLS);
  ASSERT(!pool->is_active);
  ASSERT(atomic_get(&pool->free_pages) > 0);
  switch (pool->type) {
      case GENERAL_POOL_TYPE: case DMA_POOL_TYPE:
        tlsf_allocator_init(pool);
        break;
      case BOOTMEM_POOL_TYPE:
        idalloc_init(pool);
        break;
      default:
        panic("Unknown memory pool type: %d!", pool->type);
  }

  pool->is_active = true;
}
