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
#include <arch/mem.h>
#include <mm/page.h>
#include <mm/page_alloc.h>
#include <mm/mmpool.h>
#include <mstring/panic.h>
#include <mstring/types.h>

static mmpool_t lowmem_pool, highmem_pool;

INITCODE void arch_register_mmpools(void)
{
  /* Low memory pool configuration */
  lowmem_pool.name = "Lowmem";
  lowmem_pool.flags = MMPOOL_KERN | MMPOOL_USER | MMPOOL_DMA;
  lowmem_pool.bound_addr = MB2B(4096UL);
  mmpool_register(&lowmem_pool);

  /* High memory pool configuration */
  highmem_pool.name = "Highmem";
  highmem_pool.flags = MMPOOL_KERN | MMPOOL_USER;
  highmem_pool.bound_addr = MMPOOL_BOUND_INF;
  mmpool_register(&highmem_pool);
}

INITCODE void arch_configure_mmpools(void)
{
  mmpool_t *p;

  ASSERT(atomic_get(&lowmem_pool.num_free_pages) > 0);
  highmem_pool.allocator = default_allocator;
  lowmem_pool.allocator = default_allocator;
  
  mmpool_set_preferred(PREF_MMPOOL_DMA, &lowmem_pool);
  if (atomic_get(&highmem_pool.num_free_pages) > 0) {    
    p = &highmem_pool;
  }
  else {
    p = &lowmem_pool;    
  }

  mmpool_set_preferred(PREF_MMPOOL_KERN, p);
  mmpool_set_preferred(PREF_MMPOOL_USER, p);
}
