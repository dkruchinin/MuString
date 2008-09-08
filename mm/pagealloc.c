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
 * (c) Copyright 2008 Michael Tsymbalyuk <mtzaurus@gmail.com>
 *
 * mm/pagealloc.c: Contains implementations of kernel page allocator.
 *
 */


#include <mm/mm.h>
#include <mm/pagealloc.h>
#include <eza/smp.h>
#include <eza/spinlock.h>
#include <eza/arch/types.h>
#include <mlibc/string.h>
#include <eza/arch/page.h>

EXTERN_PER_CPU(percpu_page_cache,percpu_page_cache_t);

#define LOCK_CACHE(c)
#define UNLOCK_CACHE(c)

page_frame_t *alloc_page( page_flags_t flags, int clean_page )
{
  page_frame_t *page;

  if( flags & PAF_DMA_PAGE ) {
    /* DMA page requested. */
    return NULL;
  } else {
    /* First, try to allocate from a per-CPU page cache */
    percpu_page_cache_t *cpu_cache = cpu_var(percpu_page_cache,percpu_page_cache_t);

    LOCK_CACHE(cpu_cache);
    if(cpu_cache->num_free_pages > 0 ) {
      list_head_t *l = cpu_cache->pages.next;
      list_del(l);
      cpu_cache->num_free_pages--;
      page = container_of(l, page_frame_t,active_list);
    } else {
      page = NULL;
    }
    UNLOCK_CACHE(cpu_cache);
  }

  if(clean_page) {
    arch_clean_page(page);
  }

  return page;
}

