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
 * mm/idalloc.c: Init-data memory allocator
 *
 */

#include <config.h>
#include <ds/list.h>
#include <mlibc/assert.h>
#include <mlibc/string.h>
#include <mm/page.h>
#include <mm/idalloc.h>
#include <eza/kernel.h>
#include <eza/spinlock.h>
#include <eza/arch/atomic.h>
#include <eza/arch/types.h>

/* Idalloc-specific page frame flags (_private) */
#define IDALLOC_PAGE_INUSE   0x01 /* Page is in use */
#define IDALLOC_PAGE_FULL    0x02 /* Page is full and can not be used */

idalloc_meminfo_t idalloc_meminfo;

static page_frame_t *idalloc_pages(page_idx_t npages, void *unused)
{
  page_frame_t *pages = NULL, *last_page;

  if (!(idalloc_meminfo.flags & IDALLOC_CAN_ALLOC_PAGES))
    return NULL;
  
  spinlock_lock(idalloc_meminfo.lock);
  if (idalloc_meminfo.num_avail_pages < npages)
    goto out;
  
  pages = list_entry(list_node_first(&idalloc_meminfo.avail_pages), page_frame, node);
  last_page = pages + npages - 1;
  if (last_page->_private & (IDALLOC_PAGE_INUSE | IDALLOC_PAGE_FULL)) {
    pages = NULL;
    goto out;
  }
  else {
    int i;

    for (i = 0; i < npages; i++) {
      pages[i]._private |= IDALLOC_PAGE_FULL;
      list_del(&pages[i].node);
      list_add2tail(&idalloc_meminfo.used_pages, &pages[i].node);
    }

    idalloc_meminfo.num_avail_pages -= npages;
  }
  
  out:
  spinlock_unlock(idalloc_meminfo.lock);
  return pages;
}

static void idalloc_pages_deactivate(void *unused)
{
  idalloc_meminfo.flags &= ~IDALLOC_CAN_ALLOC_PAGES;
}

void idalloc_init(mm_pool_t *pool)
{
  int i;
  page_frame_t *page;
  
  memset(&idalloc_meminfo, 0, sizeof(idalloc_meminfo));
  spinlock_initialize(&idalloc_meminfo.lock);
  list_init_head(&idalloc_meminfo.avail_pages);
  list_init_head(&idalloc_meminfo.used_pages);
  idalloc_meminfo.num_avail_pages = 0;
  for (i = 0; i < pool->total_pages; i++) {
    page = pframe_by_number(pool->first_page_id + i);
    if (page->flags & PF_RESERVED)
      continue;

    list_add2tail(&idalloc_meminfo.avail_pages, &pool->pages[i].node);
    idalloc_meminfo.num_avail_pages++;
  }

  if (CONFIG_IDALLOC_PAGES > 0) {
    page = list_entry(list_node_first(&idalloc_meminfo.avail_pages), page_frame_t, node);
    list_del(&page->node);
    list_add2tail(&idalloc_meminfo.used_pages, &page->node);
    idalloc_meminfo.num_avail_pages--;
    page->_private |= IDALLOC_PAGE_INUSE;
    idalloc_meminfo.mem = pframe_to_virt(page);
    idalloc_meminfo.flags |= IDALLOC_CAN_ALLOC_CHUNKS;
  }
  if ((idalloc_meminfo.npages - CONFIG_IDALLOC_PAGES) > 0)
    idalloc_meminfo.flags |= IDALLOC_CAN_ALLOC_PAGES;
  
  pool->allocator.alloc_pages = idalloc_pages;
  pool->allocator.alloc_pages_max_avail = NULL;
  pool->allocator.free_pages = NULL;
  pool->allocator.dump = NULL;
  pool->allocator.ctx = NULL;
  pool->type = PFA_IDALLOC;
}

void *idalloc(size_t size)
{
  void *mem = NULL;
  
  spinlock_lock(&idalloc_meminfo.lock);
  if (!(idalloc_meminfo.flags & IDALLOC_CAN_ALLOC_CHUNKS)) 
    goto out;
  else {
    page_frame_t *cur_page = PAGE_ALIGN_DOWN(idalloc_meminfo.mem);      
    uintptr_t page_bound = PAGE_ALIGN(idalloc_meminfo.mem);

    if ((uintptr_t)(idalloc_meminfo.mem + size) < page_bound) {
      /* all is ok, we can just cut size from available address */
      mem = (void *)idalloc_meminfo.mem;
      idalloc_meminfo.mem += size;
      goto out;
    }
    if (!idalloc_meminfo.num_avail_pages)
      goto out;
    
    /*
     * it seems we don't have enough memory in current page,
     * so we'll try to allocate required chunk from next page(if exists).
     * But before it we should properly deinit "full" page.
     */
    cur_page->_private &= ~IDALLOC_PAGE_INUSE;
    cur_page->_private |= IDALLOC_PAGE_FULL;    

    idalloc_meminfo.num_avail_pages--;
    cur_page = list_entry(list_node_first(&idalloc_meminfo.avail_pages), page_frame_t, node);
    cur_page->_private |= IDALLOC_PAGE_INUSE;
    list_del(&cur_page->node);
    list_add2tail(&idalloc_meminfo.used_pages, &cur_page->node);
    mem = (void *)idalloc_meminfo.mem;
    idalloc_meminfo.mem += size;
  }
  
  out:
  spinlock_unlock(&idalloc_meminfo.lock);
  return mem;
}

void idalloc_deinit(void)
{
  spinlock_lock(&idalloc_meminfo.lock);
  idalloc_meminfo.is_enabled = false;
  /*
   * FIXME DK: Don't forget to release free
   * pages that may occur unused by idalloc.
   * (they can be easily recognized by IDALLOC_PAGE_INUSE flag)
   */
  spinlock_unlock(&idalloc_meminfo.lock);
}

