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

#include <mm/page.h>
#include <mm/mmpool.h>
#include <mm/page_alloc.h>
#include <mm/vmm.h>
#include <mstring/errno.h>
#include <mstring/types.h>

#define DEFAULT_GRANULARITY 64

static page_frame_t *alloc_pages_ncont(mmpool_t *mmpool,
                                       page_idx_t num_pages, palloc_flags_t flags)
{
  mmpool_flags_t mmpool_nature = PFLAGS_MMPOOL_TYPE(flags);
  page_idx_t granularity, n;
  page_frame_t *pages = NULL, *pg;
  mmpool_t *p = mmpool;

  ASSERT(is_poerof2(mmpool_nature));
  n = num_pages;
config_granularity:
  if (p->allocator.max_block_size >= DEFAULT_GRANULARITY) {
    granularity = DEFAULT_GRANULARITY;
  }
  else {
    granularity = p->allocator.max_block_size;
  }

  while (n) {
    if (granularity > n) {
      granularity = n;
    }

    pg = mmpool_alloc_pages(mmpool, granularity);
    if (!pg) {
      if (atomic_get(&mmpool->num_free_pages)) {
        if ((granularity /= 2) != 0) {
          continue;
        }
      }

      if (p == mmpool) {
        p = get_mmpool_by_type(MMPOOL_FIRST_TYPE);
      }
      else {
        p = mmpool_next(p);
      }
      while (p) {
        if ((p != mmpool) && (p->flags & mmpool_nature)) {
          goto config_granularity;
        }

        p = mmpool_next(p);
      }

      goto failed;
    }
    if (flags & AF_ZERO) {
      pframes_memnull(pg, granularity);
    }
    if (likely(pages != NULL)) {
      list_add_range(&p->chain_node, p->chain_node.prev,
                     pages->chain_node.prev, &pages->chain_node);
    }
    else {
      pages = p;
    }

    atomic_sub(&p->num_free_pages, granularity);
    n -= granularity;
  }

  return pages;
failed:
  if (pages) {
    free_pages_chain(pages);    
  }

  return NULL;
}

page_frame_t *alloc_pages(page_idx_t num_pages, palloc_flags_t flags)
{
  mmpool_flags_t mmpool_nature;
  mmpool_t *mmpool;
  page_frame_t *pages;

  ASSERT(num_pages > 0);
  ASSERT(flags != 0);
  mmpool_nature = PAFLAGS_MMPOOL_TYPE(flags);
  if (unlikely(!is_powerof2(mmpool_nature))) {
    panic("Allocation from unrecognized memory pool with nature = %#x\n", mmpool_nature);
  }

  mmpool = get_preferred_mmpool(BITNUM(mmpool_nature));
  pages = mmpool_alloc_pages(mmpool, num_pages);
  if (!pages) {
    if (flags & AF_STRICT_CNT) {
      mmpool_t *p;

      for_each_mmpool(p) {
        if ((p == mmpool) || !(p->flags & mmpool_nature)) {
          continue;
        }

        pages = mmpool_alloc_pages(p, num_pages);
        if (pages) {
          atomic_sub(&p->num_free_pages, num_pages);
          break;
        }
      }      
      if (pages && (flags & AF_ZERO)) {
          pframes_memnull(pages, num_pages);
      }
    }
    else {
      pages = alloc_pages_ncont(mmpool, num_pages, flags);
    }
  }

  return pages;
}

void free_pages(page_frame_t *pages, page_idx_t num_pages)
{
  mmpool_t *mmpool;
  mmpool_type_t type;

  ASSERT(pages != NULL);
  ASSERT(num_pages > 0);

  mmpool = get_mmpool_by_type(PF_MMPOOL_TYPE(pages->flags));
  if (!mmpool) {
    panic("Page frame #%#x has invalid pool type %d!",
          pframe_number(pages), PF_MMPOOL_TYPE(pages->flags));
  }

  mmpool_free_pages(mmpool, pages, num_pages);
  atomic_add(&mmpool->num_free_pages, num_pages);
}

void free_pages_chain(page_frame_t *pages)
{
  list_node_t *n, *safe;

  list_for_each_safe(list_node2head(&pages->chain_node), n, safe)
    free_page(list_entry(n, page_frame_t, chain_node));

  free_page(pages);
}

uintptr_t sys_alloc_dma_pages(int num_pages)
{
  page_frame_t *pages;

  if (unlikely(!num_pages)) {
    return -EINVAL;
  }
  
  pages = alloc_pages(num_pages, DMA_POOL_TYPE | AF_ZERO);
  if (!pages) {
    pages=alloc_pages(num_pages, AF_ZERO);
    if( !pages ) {
      kprintf_fault("sys_alloc_dma_pages(): [%d:%d] failed to allocate %d pages !\n",
                    current_task()->pid,current_task()->tid,num_pages);
      return -ENOMEM;
    }
  }

  return (uintptr_t)pframe_to_phys(pages);
}

void sys_free_dma_pages(uintptr_t paddr, int num_pages)
{
  page_idx_t pidx = paddr >> PAGE_WIDTH, i;

  if (!num_pages)
    return;
  for (i = 0; i < num_pages; i++) {
    if (!page_idx_is_present(pidx + i))
      return;

    free_page(pframe_by_id(pidx + i));
  }  
}
