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
 * (c) Copyright 2009 Dan Kruchinin <dk@jarios.org>
 *
 */

#include <config.h>
#include <arch/mmpool_conf.h>
#include <mm/page.h>
#include <mm/page_alloc.h>
#include <mm/mmpool.h>
#include <mstring/types.h>

static mmpool_t lowmem_pool, highmem_pool;

INITCODE void arch_init_mmpools(void)
{
  memset(&lowmem_pool, 0, sizeof(lowmem_pool));
  lowmem_pool.name = "Lowmem (< 4G)";
  lowmem_pool.flags = MMPOOL_KERN | MMPOOL_USER | MMPOOL_DMA;
  
  memset(&highmem_pool, 0, sizeof(highmem_pool));
  highmem_pool.name = "Highmem (>= 4G)";
  highmem_pool.flags = MMPOOL_KERN | MMPOOL_USER;
}

INITCODE void arch_register_page(page_frame_t *page)
{
  mmpool_t *pool;
  
  if ((uintptr_t)pframe_to_phys(page) < MB2B(4096UL)) {
    pool = &lowmem_pool;
  }
  else {
    pool = &highmem_pool;
  }

  mmpool_add_page(pool, page);
}

INITCODE void arch_register_mmpools(void)
{
  mmpool_t *p;
  
  if (!atomic_get(&lowmem_pool.num_free_pages)) {
    panic("Memory pool \"%s\" has not free pages at all!");
  }

  mmpool_register(&lowmem_pool);
  mmpool_set_preferred(PREF_MMPOOL_DMA, &lowmem_pool);
  mmpool_register(&highmem_pool);
  if (atomic_get(&highmem_pool.num_free_pages) > 0) {    
    p = &highmem_pool;
  }
  else {
    p = &lowmem_pool;
  }

  mmpool_set_preferred(PREF_MMPOOL_KERN, p);
  mmpool_set_preferred(PREF_MMPOOL_USER, p);
}
