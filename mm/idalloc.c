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
#include <eza/spinlock.h>
#include <eza/arch/atomic.h>
#include <eza/arch/types.h>

#define IDALLOC_PAGE_INUSE 0x1

idalloc_meminfo_t idalloc_meminfo;

static void __prepare_page(page_frame_t *page)
{
  atomic_inc(&page->refcount);
  page->_private = IDALLOC_PAGE_INUSE;
}

void idalloc_enable(page_frame_t *pages)
{
  page_frame_t *last_frame;
  
  memset(&idalloc_meminfo, 0, sizeof(idalloc_meminfo));
  spinlock_initialize(&idalloc_meminfo.lock, "");
  list_init_head(&idalloc_meminfo.avail_pages);
  if (IDALLOC_PAGES <= 0)
    return;

  last_frame = pframe_by_number(pframe_number(pages) + IDALLOC_PAGES - 1);
  list_del_range(&pages->node, &last_frame->node);
  list_add_range(&pages->node, &last_frame->node,
                 list_node_first(&idalloc_meminfo.avail_pages),
                 list_node_last(&idalloc_meminfo.avail_pages));
  __prepare_page(pages);
  idalloc_meminfo.pages = IDALLOC_PAGES;
  idalloc_meminfo.mem = pframe_to_virt(pages);
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
    if (list_is_empty(&idalloc_meminfo.avail_pages))
      goto out;

    cur_page = list_entry(list_node_first(&idalloc_meminfo.avail_pages), page_frame_t, node);
    __prepare_page(cur_page);
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
