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
 * mm/pfalloc.c: page frame allocation API
 *
 */

#include <mm/page.h>
#include <mm/mmpool.h>
#include <mm/pfalloc.h>
#include <mm/vmm.h>
#include <mstring/errno.h>
#include <mstring/types.h>
#include <mm/tlsf.h>

#define DEFAULT_GRANULARITY 64

static page_frame_t *__alloc_pages_ncont(mm_pool_t *pool, page_idx_t n, pfalloc_flags_t flags)
{
  page_idx_t granularity;
  page_frame_t *pages = NULL, *p;

  config_granularity:
  if (pool->allocator.max_block_size >= DEFAULT_GRANULARITY)
    granularity = DEFAULT_GRANULARITY;
  else
    granularity = pool->allocator.max_block_size;
  
  while (n) {    
    if (granularity > n)
      granularity = n;

    p = mmpool_alloc_pages(pool, granularity);
    if (!p) {
      if (atomic_get(&pool->free_pages)) {
        if ((granularity /= 2) != 0)
          continue;
      }
      switch (pool->type) {
          case HIGHMEM_POOL_TYPE:
            pool = POOL_GENERAL();
            goto config_granularity;
          case GENERAL_POOL_TYPE:
            if (!POOL_DMA()->is_active)
              goto failed;

            pool = POOL_DMA();
            goto config_granularity;
          default:
            goto failed;
      }
    }
    if ((flags & AF_ZERO) && (pool->type != HIGHMEM_POOL_TYPE))
      pframes_memnull(p, granularity);
    if (unlikely(pages == NULL))
      pages = p;
    else {
      list_add_range(&p->chain_node, p->chain_node.prev,
                     pages->chain_node.prev, &pages->chain_node);
    }

    atomic_sub(&pool->free_pages, granularity);
    n -= granularity;
  }

  return pages;

  failed:
  if (pages)
    free_pages_chain(pages);

  return NULL;
}

page_frame_t *alloc_pages(page_idx_t n, pfalloc_flags_t flags)
{
  page_frame_t *pages = NULL;
  mm_pool_t *pool;

  pool = get_mmpool_by_type(flags & MMPOOLS_MASK);
  ASSERT(n > 0);
  if (!pool) {
    kprintf("No pool %d\n", flags & MMPOOLS_MASK);
    return NULL;
  }

  if (!pool->is_active) {
    kprintf(KO_WARNING "alloc_pages: Can't allocate from pool \"%s\" (pool is no active)\n", pool->name);
    return NULL;
  }
  if (!(flags & AF_USER)) {
    pages = mmpool_alloc_pages(pool, n);
    if (!pages) {
      if ((pool->type == GENERAL_POOL_TYPE) && POOL_DMA()->is_active) {
        /* try to allocate pages from DMA pool if it's possible */
        pool = POOL_DMA();
        pages = mmpool_alloc_pages(pool, n);
        if (!pages)
          goto out;
      }
      else
        goto out;
    }
    if (flags & AF_ZERO)
      pframes_memnull(pages, n);

    atomic_sub(&pool->free_pages, n);
  }
  else {
    /* Allocate non-contious chain of pages... */
    if (pool->type == HIGHMEM_POOL_TYPE) {
      pages = mmpool_alloc_pages(pool, n);
      if (pages)
        goto out;
    }

    pages = __alloc_pages_ncont(pool, n, flags);
  }

  /* done */
out:
  return pages;
}

void free_pages(page_frame_t *pages, page_idx_t num_pages)
{
  mm_pool_t *pool = get_mmpool_by_type(pages->pool_type);

  if (!pool)
    panic("Page frame #%#x has invalid pool type %d!", pframe_number(pages), pages->pool_type);
  if (!pool->is_active) {
    kprintf(KO_ERROR "free_pages: trying to free pages from dead pool %s\n", pool->name);
    return;
  }

  mmpool_free_pages(pool, pages, num_pages);
  atomic_add(&pool->free_pages, num_pages);
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

  return (uintptr_t)pframe_phys_addr(pages);
}

void sys_free_dma_pages(uintptr_t paddr, int num_pages)
{
  page_idx_t pidx = paddr >> PAGE_WIDTH, i;

  if (!num_pages)
    return;
  for (i = 0; i < num_pages; i++) {
    if (!page_idx_is_present(pidx + i))
      return;

    free_page(pframe_by_number(pidx + i));
  }  
}
