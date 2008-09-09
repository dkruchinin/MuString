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
 * mm/mm.c: Contains implementation of kernel memory manager.
 *
 */


#include <mlibc/kprintf.h>
#include <eza/kernel.h>
#include <eza/arch/page.h>
#include <mm/mm.h>
#include <eza/swks.h>
#include <eza/list.h>
#include <eza/smp.h>

extern uint8_t e820count;
static uint64_t min_phys_addr, max_phys_addr;
static page_idx_t dma_pages;

/* Here they are ! */
page_frame_t *page_frames_array;
memory_zone_t memory_zones[NUM_MEMORY_ZONES];
percpu_page_cache_t PER_CPU_VAR(percpu_page_cache);

static void detect_physical_memory(void) {
  int idx, found;
  kprintf( "E820 memory map:\n" ); 
  char *types[] = { "(unknown)", "(usable)", "(reserved)", "(ACPI reclaimable)",
                    "(ACPI non-volatile)", "(BAD)" };

  for( idx = 0, found = 0; idx < e820count; idx++ ) {
    e820memmap_t *mmap = &e820table[idx];
    uint64_t length = ((uintptr_t)mmap->length_high << 32) | mmap->length_low;
    char *type;

    if( mmap->type <= 5 ) {
      type = types[mmap->type];
    } else {
      type = types[0];
    }

    kprintf( " BIOS-e820: 0x%.16x - 0x%.16x %s\n",
             mmap->base_address, mmap->base_address + length, type );
    
    if( !found && mmap->base_address == KERNEL_PHYS_START && mmap->type == 1 ) {
      min_phys_addr = 0;
      max_phys_addr = mmap->base_address + length;
      found = 1;
    }
  }

  if( !found ) {
    panic( "detect_physical_memory(): No valid E820 memory maps found for main physical memory area !\n" );
  }

  if( max_phys_addr <= min_phys_addr ||
      ((min_phys_addr - max_phys_addr) <= MIN_PHYS_MEMORY_REQUIRED )) {
    panic( "detect_physical_memory(): Insufficient E820 memory map found for main physical memory area !\n" );
  }

  /* Setup DMA zone. */
  dma_pages = MB(16) / PAGE_SIZE;
}

static void reset_page_frame(page_frame_t *page)
{
  init_list_head(&page->active_list);
  page->flags &= PERMANENT_PAGE_FLAG_MASK;  /* Reset all flags but special ones. */
  atomic_set(&page->refcount,0);
}

static void initialize_page_array(void)
{
  int just_left, e820id = 0;
  uint64_t curr_addr = 0;
  uint64_t mmap_begin, mmap_end;
  e820memmap_t *mmap = &e820table[0];   /* Start from the first memory area. */
  page_idx_t page_idx = 0;
  memory_zone_t *zone;
  uint32_t type;
  page_frame_t *page;
  list_head_t *page_list;
  uint32_t flags;
  page_idx_t kernel_start_page, kernel_end_page;

  /* Setup the pointer to the array. */
  page_frames_array = (page_frame_t*)KERNEL_FIRST_FREE_ADDRESS;

  /* Calculate kernel image size + page frame array itself. */
  kernel_end_page = ((uintptr_t)KERNEL_FIRST_FREE_ADDRESS - KERNEL_BASE - KERNEL_PHYS_START) / PAGE_SIZE;
  kernel_end_page += ((max_phys_addr / PAGE_SIZE) * sizeof(page_frame_t)) / PAGE_SIZE;

  kernel_start_page = KERNEL_PHYS_START / PAGE_SIZE;
  kernel_end_page += kernel_start_page; /* Till this 'kernel_end_page' has kept only offset. */

  mmap_begin = mmap->base_address;
  mmap_end = mmap->base_address + (((uintptr_t)(mmap->length_high) << 32) | mmap->length_low);
  mmap_end &= PAGE_ADDR_MASK;
  type = mmap->type;

  just_left = 0;
  while( curr_addr < max_phys_addr ) {
    /* Locate zone current page belongs to. */
    if( page_idx < dma_pages ) {
      zone = &memory_zones[ZONE_DMA];
    } else {
      zone = &memory_zones[ZONE_NORMAL]; 
    }

    flags = zone->type; /* Initial page flags. */

    if( curr_addr >= mmap_begin && curr_addr < mmap_end ) {
      if(just_left != 0) {
        just_left = 0;
      }

      if(type != E820_USABLE) {
        page_list = &zone->reserved_pages;
        flags |= PAGE_RESERVED;
      } else {
        page_list = &zone->pages;
      }
    } else {
      /* Has just left the map ? */
      if( just_left == 0 ) {
        just_left = 1;

        if(e820id < e820count) {
          /* Switch to the next E820 map. */
          mmap = &e820table[++e820id];
          mmap_begin = mmap->base_address;
          mmap_end = mmap->base_address + (((uintptr_t)mmap->length_high << 32) | mmap->length_low);
          mmap_begin &= PAGE_ADDR_MASK;
          mmap_end &= PAGE_ADDR_MASK;

          type = mmap->type;
        } else {
          /* Hmmmmm, no more E820 maps ? */
          mmap_begin = mmap_end = 0;
        }
       /* Since we have just switched to the next map, we should rescan current page. */
        continue;
      } else {
        /* No valid E820 map for this page even if we've moved to the next record in
         * the E820 array, so we assume that current page is reserved.
         */
        flags |= PAGE_RESERVED;
        page_list = &zone->reserved_pages;
      }
    }

    /* Make sure we've taken into account kernel pages. */
    if( page_idx >= kernel_start_page && page_idx <= kernel_end_page ) {
        flags |= PAGE_RESERVED;
        page_list = &zone->reserved_pages;
    }

    /* Initialize current page. */
    page = &page_frames_array[page_idx];
    reset_page_frame(page);
    page->idx = page_idx;
    page->flags = flags;

    /* Insert page into the list. */
    list_add_tail(&page->active_list,page_list);
    zone->num_total_pages++;
    if(flags & PAGE_RESERVED) {
      zone->num_reserved_pages++;
     } else {
      zone->num_free_pages++;
    }

    page_idx++;
    curr_addr += PAGE_SIZE;
  }
}

static void initialize_zones(void)
{
  memory_zone_type_t types[NUM_MEMORY_ZONES] = {ZONE_DMA,ZONE_NORMAL};
  int idx;

  for(idx = 0; idx < NUM_MEMORY_ZONES; idx++ ) {
    memory_zone_t *zone = &memory_zones[idx];

    zone->type = types[idx];
    zone->num_total_pages = zone->num_free_pages = zone->num_reserved_pages = 0;
    init_list_head(&zone->pages);
    init_list_head(&zone->reserved_pages);
    spinlock_initialize(&zone->lock, "" );
  }
}

static void setup_memory_swks(void)
{
  swks.mem_total_pages = max_phys_addr / PAGE_SIZE;
}

static void initialize_percpu_caches(void)
{
  percpu_page_cache_t *cache;
  memory_zone_t *zone = &memory_zones[ZONE_NORMAL];
  page_idx_t cpupages = zone->num_total_pages / NR_CPUS;

  for_each_percpu_var(cache,percpu_page_cache) {
    page_idx_t p = 0;

    spinlock_initialize(&cache->lock, "<percpu cache spinlock>");
    init_list_head(&cache->pages);

    /* Take into account the last cache. */
    if( cpupages + NR_CPUS >= zone->num_total_pages ) {
      cpupages = zone->num_total_pages;
    }

    /* Now move all pages to the cache. */
    for(p = 0; p < cpupages && (zone->pages.next != &zone->pages); p++ ) {
      list_head_t *l = zone->pages.next;
      list_del(l);
      list_add_tail(l,&cache->pages);
      zone->num_free_pages--;
      zone->num_total_pages--;
      p++;
    }

    cache->num_free_pages = cache->total_pages = p;
  }
}

void build_page_array(void)
{
  if( e820count < 0xff ) {
    detect_physical_memory();

    kprintf( "Initializing memory ... " );
    initialize_zones();    
    initialize_page_array();

    initialize_percpu_caches();

    kprintf( "Done (%d pages initialized).\n", max_phys_addr / PAGE_SIZE );
    setup_memory_swks();
  } else {
    panic( "build_page_array(): No valid E820 memory maps found !\n" );
  }
}

