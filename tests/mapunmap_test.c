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
 * (c) Copyright 2009 Dan Kruchinin <dan.kruchinin@gmail.com>
 *
 * tests/mapunmap_test.c: General map/unmap test
 *
 */

#include <test.h>
#include <ds/iterator.h>
#include <mm/page.h>
#include <mm/mmpool.h>
#include <mm/pfalloc.h>
#include <mm/vmm.h>
#include <mm/pfi.h>
#include <eza/task.h>
#include <kernel/syscalls.h>
#include <mlibc/types.h>

#define MAPUNMAP_TEST_ID "Map/Unmap test"
#define TC_MAP_ADDR PAGE_ALIGN((KERNEL_BASE + ((ulong_t)num_phys_pages << PAGE_WIDTH) * 2))

static bool is_completed = false;

static void __check_mapped(test_framework_t *tf, uintptr_t start_addr,
                           ulong_t num_pages, ulong_t start_idx)
{
  page_idx_t idx = start_idx;
  page_frame_iterator_t pfi;
  ITERATOR_CTX(page_frame, PF_ITER_PTABLE) pfi_ptable_ctx;

  pfi_ptable_init(&pfi, &pfi_ptable_ctx, &kernel_rpd, start_addr, num_pages);
  iterate_forward(&pfi) {
    if (pfi.pf_idx != idx) {
      tf->printf("Mapping %p -> %p: Expected page index %d, but got %d\n",
                 start_addr, start_addr + (num_pages << PAGE_WIDTH), start_idx, pfi.pf_idx);
      tf->abort();
    }

    idx++;
  }
  if ((idx - start_idx) != num_pages) {
    tf->printf("%d - %d != %d\n", idx, start_idx, num_pages);
    tf->abort();
  }
}

static void tc_map_unmap_core(void *ctx)
{
  test_framework_t *tf = ctx;
  mm_pool_t *pool = mmpools_get_pool(POOL_GENERAL);
  ulong_t num_pages = pool->allocator.block_sz_max - 1;
  int ret;
  page_frame_t *pages;
  
  pages = alloc_pages(num_pages, AF_PGEN);  
  if (!pages) {
    tf->printf("Failed to allocate %d pages!\n", num_pages);
    tf->abort();
  }
  
  tf->printf("Creating mapping: %p -> %p\n", TC_MAP_ADDR, TC_MAP_ADDR + (num_pages << PAGE_WIDTH));
  ret = mmap_kern(TC_MAP_ADDR, pframe_number(pages), num_pages, KMAP_KERN | KMAP_READ | KMAP_WRITE);
  if (ret) {
    tf->printf("Failed to map %d pages from %p to %p\n",
               atomic_get(&pool->free_pages), TC_MAP_ADDR, TC_MAP_ADDR + (num_pages << PAGE_WIDTH));
    tf->abort();

  }

  __check_mapped(tf, TC_MAP_ADDR, num_pages, pframe_number(pages));
  tf->printf("Unmap mapped pages range [%p -> %p]\n", TC_MAP_ADDR, TC_MAP_ADDR + (num_pages << PAGE_WIDTH));
  munmap_kern(TC_MAP_ADDR, num_pages);
  tf->printf("Done\n");
  is_completed = true;
  sys_exit(0);
}

static void tc_run(test_framework_t *tf, void *unused)
{
  if (kernel_thread(tc_map_unmap_core, tf, NULL)) {
    tf->printf("Can't create kernel thread!");
    tf->abort();
  }

  tf->test_completion_loop(MAPUNMAP_TEST_ID, &is_completed);
}

static bool tc_initialize(void **unused)
{
  return true;
}

static void tc_deinitialize(void *unused)
{
}


testcase_t mapunmap_tc = {
  .id = MAPUNMAP_TEST_ID,
  .initialize = tc_initialize,
  .run = tc_run,
  .deinitialize = tc_deinitialize,
};
