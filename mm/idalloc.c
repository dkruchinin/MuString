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

#define IDALLOC_PAGE       0x01
#define IDALLOC_PAGE_INUSE 0x02
#define IDALLOC_PAGE_FULL  0x04

idalloc_meminfo_t idalloc_meminfo;

static void __prepare_page(page_frame_t *page)
{
  page->flags |= PF_RESERVED;
  page->_private = IDALLOC_PAGE;
}

void idalloc_enable(mm_pool_t *pool)
{
  int i, npages = 0;
  page_frame_t *first_page;
  
  memset(&idalloc_meminfo, 0, sizeof(idalloc_meminfo));
  spinlock_initialize(&idalloc_meminfo.lock, "");
  list_init_head(&idalloc_meminfo.avail_pages);
  list_init_head(&idalloc_meminfo.used_pages);
  if (IDALLOC_PAGES <= 0)
    return;

  for (i = 0; i < pool->total_pages; i++) {
    if (pool->pages[i].flags & PF_RESERVED)
      continue;
    __prepare_page(pool->pages + i);
    list_add2tail(&idalloc_meminfo.avail_pages, &pool->pages[i].node);
    pool->reserved_pages++;
    if (++npages == IDALLOC_PAGES)
      break;
  }
  if (npages != IDALLOC_PAGES) {
    panic("idalloc_enable: Can't get %d pages for init-data allocator from pool %s",
          IDALLOC_PAGES, mmpools_get_pool_name(pool->type));
  }

  atomic_sub(&pool->free_pages, IDALLOC_PAGES);
  first_page = list_entry(list_node_first(&idalloc_meminfo.avail_pages), page_frame_t, node);
  first_page->_private |= IDALLOC_PAGE_INUSE;
  idalloc_meminfo.npages = IDALLOC_PAGES;
  idalloc_meminfo.mem = pframe_to_virt(first_page);
  idalloc_meminfo.is_enabled = true;
}

void *idalloc(size_t size)
{
  void *mem = NULL;
  
  spinlock_lock(&idalloc_meminfo.lock);
  if (!idalloc_is_enabled() || list_is_empty(&idalloc_meminfo.avail_pages))
    goto out;
  else {
    page_frame_t *cur_page =
      list_entry(list_node_first(&idalloc_meminfo.avail_pages), page_frame_t, node);
    uintptr_t page_bound = (uintptr_t)pframe_to_virt(cur_page) + PAGE_SIZE;

    if ((uintptr_t)(idalloc_meminfo.mem + size) < page_bound) {
      mem = (void *)idalloc_meminfo.mem;
      idalloc_meminfo.mem += size;
      goto out;
    }

    list_delfromhead(&idalloc_meminfo.avail_pages);
    cur_page->_private &= ~IDALLOC_PAGE_INUSE;
    cur_page->_private |= IDALLOC_PAGE_FULL;
    list_add2tail(&idalloc_meminfo.used_pages, &cur_page->node);
    if (list_is_empty(&idalloc_meminfo.avail_pages))
      goto out;

    cur_page = list_entry(list_node_first(&idalloc_meminfo.avail_pages), page_frame_t, node);
    cur_page->_private |= IDALLOC_PAGE_INUSE;
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