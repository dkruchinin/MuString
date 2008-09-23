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
 * mm/mmpool.c: MM-pools
 *
 */

#include <config.h>
#include <ds/list.h>
#include <mlibc/string.h>
#include <mm/mmpool.h>
#include <mm/page.h>
#include <mm/tlsf.h>
#include <eza/kernel.h>
#include <eza/arch/atomic.h>
#include <eza/arch/types.h>

mm_pool_t mm_pools[NOF_MM_POOLS];

void mmpools_init(void)
{
  mm_pool_type_t t;
  
  memset(mm_pools, 0, sizeof(*mm_pools) * NOF_MM_POOLS);
  for (t = __POOL_FIRST; t < NOF_MM_POOLS; t++) {
    mm_pools[t].type = t;
    atomic_set(&mm_pools[t].free_pages, 0);
  }
}

void mmpools_add_page(page_frame_t *page)
{
  mm_pool_t *pool = mmpools_get_pool(pframe_pool_type(page));

  if (!pool->pages) {
    pool->pages = page;
    if (pool->type == POOL_GENERAL) {
      kprintf("PGP: ==> %d\n", pframe_number(pool->pages));
    }    
    pool->is_active = true;
  }
  
  if (page->flags & PF_RESERVED) {
    pool->reserved_pages++;
    list_add2tail(&pool->reserved, &page->node);
  }
  else
    atomic_inc(&pool->free_pages);

  pool->total_pages++;
}

void mmpools_init_pool_allocator(mm_pool_t *pool)
{
  switch (pool->type) {
      case POOL_GENERAL:
        tlsf_alloc_init(pool);
        break;
      case POOL_DMA:
        break; /* FIXME DK: */
      default:
        panic("Unlnown memory pool type: %d", pool->type);
  }
}

void mmpools_check_pool_pages(mm_pool_t *pool)
{
  char *pool_name = mmpools_get_pool_name(pool->type);
  page_idx_t prev;
  page_frame_t *page;
  bool ok = true;

  if (!pool->pages)
    return;

  prev = pframe_number(list_entry(list_node_first(&pool->pages->head),
                                  page_frame_t, node));
  kprintf("Checking pages in memory pool %s...", pool_name);
  list_for_each_entry(&pool->pages->head, page, node) {
    if ((page->idx - prev) > 1) {
      ok = false;
      break;
    }

    prev = pframe_number(page);
  }
  if (ok) {
    kprintf("   [OK]\n");
    return;
  }

  kprintf("  [FAILED]\n");
  kprintf("  ATTENTION! Pool %s contains non-continous pages!\n", pool_name);
}
