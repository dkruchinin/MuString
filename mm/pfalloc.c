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
 * mm/pfalloc.c: page frame allocation API
 *
 */

#include <mm/page.h>
#include <mm/mmpool.h>
#include <mm/pfalloc.h>
#include <mm/vmm.h>
#include <eza/errno.h>
#include <mlibc/types.h>
#include <mm/tlsf.h>

page_frame_t *alloc_pages(page_idx_t n, pfalloc_flags_t flags)
{
  page_frame_t *pages = NULL;
  mm_pool_t *pool;

  if (!(flags & PAGE_POOLS_MASK))
    flags |= AF_PGEN;

  pool = mmpools_get_pool(__pool_type(flags & PAGE_POOLS_MASK));
  if (!pool->is_active) {
    kprintf(KO_WARNING "alloc_pages: Can't allocate from pool \"%s\" (pool is no active)\n",
            mmpools_get_pool_name(pool->type));
  }
  if (atomic_get(&pool->free_pages) < n)
    goto out;
  
  pages = __pool_alloc_pages(pool, n);
  if (!pages)
    goto out;
  if (flags & AF_ZERO) /* fill page block with zeros */
    pframe_memnull(pages, n);
  if (flags & AF_CLEAR_RC) { /* zero all refcounts of pages in a block */
    register page_idx_t i;

    for (i = 0; i < n; i++)
      atomic_set(&pages[i].refcount, 0);
  }

  /* done */
  out:
  return pages;
}

void free_pages(page_frame_t *pages, page_idx_t num_pages)
{
  mm_pool_t *pool = mmpools_get_pool(pframe_pool_type(pages));

  if (!pool->is_active) {
    kprintf(KO_ERROR "free_pages: trying to free pages from dead pool %s\n",
            mmpools_get_pool_name(pool->type));
    return;
  }
  
  __pool_free_pages(pool, pages, num_pages);
}

page_idx_t pages_block_size(page_frame_t *first_page)
{
  mm_pool_t *pool = mmpools_get_pool(pframe_pool_type(first_page));

  if (!pool->is_active) {
    kprintf(KO_ERROR "pages_block_get_size: Trying to get size of pages "
            "block owned by inactive memory pool %s\n", mmpools_get_pool_name(pool->type));
    return -EINVAL;
  }

  return __pool_pblock_size(pool, first_page);
}

/*
 * FIXME DK: for now alloc_pages_ncont can allocate more pages than
 * target allocator's max pages limit, but it assumes that there are
 * enough free blocks of max size. This is not good behaviour, because
 * actually allocator may not have any free blocks of max size, but
 * its memory may be highly fragmented, so there may be enough pages
 * in blocks less than max ones. That *must* be attended.
 */
page_frame_t *alloc_pages_ncont(page_idx_t npages, pfalloc_flags_t flags)
{
  page_frame_t *pages = NULL;  
  page_idx_t block_sz, max_sz;
  page_frame_t *ap;

  if (!(flags & PAGE_POOLS_MASK))
    flags |= AF_PGEN;

  max_sz = __pool_block_size_max(mmpools_get_pool(POOL_GENERAL)) - 1;
  while (npages) {
    block_sz = (npages > max_sz) ? max_sz : npages;
    ap = alloc_pages(block_sz, flags);
    if (!ap)
      goto free_pages;
    if (unlikely(!pages)) {
      pages = ap;
      list_init_head(&pages->head);
      list_add2tail(&pages->head, &pages->node);
    }
    else
      list_add2tail(&pages->head, &ap->node);
    
    npages -= block_sz;
  }

  goto out;
  free_pages:
  if (pages) {
    list_node_t *iter, *safe;
    page_frame_t *pf;  

    list_for_each_safe(&pages->head, iter, safe) {
      pf = list_entry(iter, page_frame_t, node);
      free_pages(pf, pages_block_size(pf));
    }

    pages = NULL;
  }
  
  out:
  return pages;
}

void free_pages_ncont(page_frame_t *pages)
{
  list_node_t *n, *safe;
  page_frame_t *pf;

  list_for_each_safe(&pages->head, n, safe) {
    pf = list_entry(n, page_frame_t, node);
    free_pages(pf, pages_block_size(pf));
  }
}
