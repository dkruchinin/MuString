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


#include <ds/list.h>
#include <mlibc/assert.h>
#include <mm/mm.h>
#include <mm/pagealloc.h>
#include <eza/smp.h>
#include <eza/spinlock.h>
#include <eza/arch/types.h>
#include <mlibc/string.h>
#include <eza/arch/page.h>

//static percpu_page_cache_t PER_CPU_VAR(percpu_page_cache);

#define LOCK_CACHE(c)
#define UNLOCK_CACHE(c)

page_frame_t *alloc_page( page_flags_t flags, int clean_page )
{
  page_frame_t *page;

  //if( flags & PAF_DMA_PAGE ) {
    /* DMA page requested. */
    //return NULL;
  //} else {
    /* First, try to allocate from a per-CPU page cache */      
    /*percpu_page_cache_t *cpu_cache = percpu_get_var(percpu_page_cache);

    LOCK_CACHE(cpu_cache);    
    if(cpu_cache->num_free_pages > 0 ) {
      list_node_t *l = list_node_first(&cpu_cache->pages);
      list_del(l);
      cpu_cache->num_free_pages--;
      page = list_entry(l, page_frame_t, page_next);
    } else {
      page = NULL;
    }
    UNLOCK_CACHE(cpu_cache);
  }

  if(clean_page) {
    arch_clean_page(page);
    }*/

  return page;
}
