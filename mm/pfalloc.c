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
#include <mm/page.h>
#include <mm/mmpool.h>
#include <mm/pfalloc.h>
#include <eza/arch/types.h>

page_frame_t *alloc_pages(page_idx_t n, pfalloc_flags_t flags)
{
  page_frame_t *pages = NULL;
  mm_pool_t *pool;
  page_idx_t i;

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
  for (i = 0; i < n; i++)
    atomic_set(&pages[i].refcount, 1);

  /* done */
  out:
  return pages;
}

void free_pages(page_frame_t *pages)
{
  mm_pool_t *pool = mmpools_get_pool(pframe_pool_type(pages));

  if (!pool->is_active) {
    kprintf(KO_ERROR "free_pages: trying to free pages from dead pool %s\n",
            mmpools_get_pool_name(pool->type));
    return;
  }
  
  __pool_free_pages(pool, pages);
}

status_t pages_block_size(page_frame_t *first_page)
{
  mm_pool_t *pool = mmpools_get_pool(pframe_pool_type(first_page));

  if (!pool->is_avtive) {
    kprintf(KO_ERROR "pages_block_get_size: Trying to get size of pages "
            "block owned by inactive memory pool %s\n", mmpools_get_pool_name(pool->type));
    return -EINVAL;
  }

  return __pool_pblock_size(pool, first_page);
}

#ifdef CONFIG_ZERO_USER_PAGES
#define UPAGES_DEFAULT_FLAGS (AF_PGEN)
#else
#define UPAGES_DEFAULT_FLAGS (AF_PGEN | AF_ZERO)
#endif /* CONFIG_ZERO_USER_PAGES */

/* FIXME DK: allocate pages regarding to memory limits of a particular task
 * There is also a good issue when each process mm may have an opportunity to
 * allocate pages from a different pools. For example from general, dma and/or cram
 * pools. Each such pool should be able to restricted by the root user.
 */
page_frame_t *alloc_pages4uspace(vmm_t *vmm, page_idx_t npages)
{
  page_frame_t *pages = NULL;
  
  if ((vmm->num_pages + npages) >= vmm->max_pages)
    goto out;
  else {
    page_idx_t block_sz, max_sz;
    page_frame_t *ap;

    max_sz = __pool_block_size_max(mmpools_get_pool(POOL_GENERAL)) - 1;
    while (npages) {
      block_sz = (npages > max_sz) ? max_sz : npages;
      ap = alloc_pages(block_sz, UPAGES_DEFAULT_FLAGS);
      if (!ap)
        goto free_pages;
      if (unlikely(!pages)) {
        pages = ap;
        list_init_head(&pages->head);
      }
      else
        list_add2tail(&pages->head, &ap->node);

      npages -= block_sz;
    }

    goto out;
  }

  free_pages:
  if (pages) {
    list_node_t *iter, *safe;

    list_for_each_safe(&pages->head, iter, safe)
      free_pages(list_entry(iter, page_frame_t, node));

    free_pages(pages);
  }
  
  out:
  return pages;
}
