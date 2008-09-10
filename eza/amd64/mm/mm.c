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
 * eza/amd64/mm/mm.c: Implementations of routines for initial memory remapping
 *                    using 4K pages (AMD64-specific).
 *
 */

#include <mm/mm.h>
#include <mm/pagealloc.h>
#include <eza/arch/mm.h>
#include <eza/arch/page.h>
#include <eza/smp.h>
#include <eza/swks.h>
#include <mlibc/kprintf.h>
#include <mm/pt.h>
#include <eza/swks.h>
#include <eza/arch/asm.h>
#include <eza/kernel.h>
#include <mlibc/string.h>
#include <eza/pageaccs.h>

extern percpu_page_cache_t percpu_page_cache_cpu_0;

/* Initial kernel top-level page directory record. */
page_directory_t kernel_pt_directory;
uint8_t k_entries[PAGE_SIZE] __attribute__((aligned(PAGE_SIZE)));

static void initialize_kernel_page_directory(void)
{
  initialize_page_directory(&kernel_pt_directory);
  kernel_pt_directory.entries = k_entries;
}

static void verify_mapping( char *zone_name, uintptr_t start_addr, page_idx_t num_pages,
                            page_idx_t start_idx ) {
  page_idx_t i, t;
  char *ptr = (char *)start_addr;
  int good = 1;

  kprintf( "Verifying zone (%s) ... ", zone_name );
  for( i = 0; i < num_pages; i++ ) {
    t = mm_pin_virtual_address(&kernel_pt_directory,(uintptr_t)ptr);
    if( t != start_idx ) {
      good = 0;
      break;
    }

    start_idx++;
    ptr += PAGE_SIZE;
  }

  if( good ) {
    kprintf( " OK\n" );
  } else {
    kprintf( " FAIL" );
    kprintf( "\n[!!!] 0x%X: page mismatch ! found idx: 0x%X, expected: 0x%X\n",
             ptr, t, start_idx );
    panic( "arch_remap_memory(): Can't remap zone !" );
  }
  return;
}

void arch_remap_memory(cpu_id_t cpu)
{
  if( cpu == 0 ) {
    int r;
    pageaccs_linear_pa_ctx_t pa_ctx;

    /* Initialize context. */
    pa_ctx.start_page = 1;
    pa_ctx.end_page = swks.mem_total_pages;

    pageaccs_linear_pa.reset(&pa_ctx);

    /* First, initialize kernel default page-table directory. */
    initialize_kernel_page_directory();

    kprintf( "PGD: Virt = 0x%x, Phys = 0x%x\n",
             kernel_pt_directory.entries, virt_to_phys(kernel_pt_directory.entries) );

    /* Next, we'll create direct mapping 'one-to-one' for the first 16MBs of memory.
     * This is needed for example, for VGA console.
     * We intentionally skip page number zero since it will allow us to detect
     * kernel-mode NULL pointers bugs in runtime.
     */
    r = __mm_map_pages( &pageaccs_linear_pa,0x1000,
                        memory_zones[ZONE_DMA].num_total_pages-1,0, &pa_ctx );
    if( r != 0 ) {
      panic( "arch_remap_memory(): Can't remap physical pages (DMA identical mapping) !" );
    }

    verify_mapping( "DMA zone",0x1000,memory_zones[ZONE_DMA].num_total_pages-1,1 );

    /* Now we should remap all available physical memory starting at 'KERNEL_BASE'. */
    pa_ctx.start_page = 0;
    pageaccs_linear_pa.reset(&pa_ctx);
    r = __mm_map_pages( &pageaccs_linear_pa, KERNEL_BASE,
                        swks.mem_total_pages, 0, &pa_ctx );
    if( r != 0 ) {
      panic( "arch_remap_memory(): Can't remap physical pages !" );
    }

    /* Verify that mappings are valid. */
    verify_mapping( "Non-DMA zone", KERNEL_BASE, swks.mem_total_pages, 0 );
   }  

  /* All CPUs must initially reload their CR3 registers with already
   * initialized Level-4 page directory.
   */
  load_cr3( _k2p((uintptr_t)&kernel_pt_directory.entries[0]), 1, 1 );

  kprintf( "CPU #%d: All physical memory was successfully remapped.\n", cpu );
}

/* AMD 64-specific function for zeroizing a page. */
void arch_clean_page(page_frame_t *frame)
{
  memset( pframe_to_virt(frame), 0, PAGE_SIZE );
}

