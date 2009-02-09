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
 * tests/tlsf_test.c: TLSF O(1) page allocator main test
 *
 */

#include <test.h>
#include <ds/list.h>
#include <mm/page.h>
#include <mm/pfalloc.h>
#include <mm/mmpool.h>
#include <mm/vmm.h>
#include <mm/tlsf.h>
#include <eza/task.h>
#include <kernel/syscalls.h>
#include <mlibc/types.h>

#define TLSF_TEST_ID "TLSF test"

static int __pr = 0;

static struct tlsf_test_ctx {
  mm_pool_t *pool;
  tlsf_t *tlsf;
  bool completed;
} tlsf_ctx;

static inline int __simple_random(void)
{
  __pr =  (666 * __pr + 0xdead) % 974;
  return __pr;
}

static page_idx_t __allocate_all(test_framework_t *tf, page_frame_t **plst)
{
  page_frame_t *pages_start = NULL, *pages;
  page_idx_t max_block_size = tlsf_ctx.pool->allocator.block_sz_max;
  page_idx_t allocated = 0;

  tf->printf("Allocating all possible pages (max block size = %d pages)\n", max_block_size);
  for (;;) {
    pages = alloc_pages(max_block_size - 1, AF_PGEN);
    if (!pages)
      break;

    allocated += max_block_size - 1;
    if (!pages_start) {
      list_init_head(&pages->head);      
      pages_start = pages;
    }

    list_add2tail(&pages_start->head, &pages->node);
  }
  if (!allocated) {
    kprintf("Something wrong with TLSF...\n");
    tf->failed();
  }
  if (tlsf_ctx.pool->free_pages) {
    allocated += tlsf_ctx.pool->free_pages;
    pages = alloc_pages(tlsf_ctx.pool->free_pages, AF_PGEN);
    if (!pages) {
      kprintf("Failed to allocated %d last available pages.\n", tlsf_ctx.pool->free_pages);
      tf->failed();
    }
    
    list_add2tail(&pages_start->head, &pages->node);
  }
  
  *plst = pages_start;
  return allocated;
}

#define MAX_PGS_NCONT 128

/* TODO DK: check automatically if all merges were done properly! */
static void tc_alloc_dealloc(void *ctx)
{
  test_framework_t *tf = ctx;  
  list_node_t *iter, *safe;
  page_idx_t allocated, num, i, saved_ap;
  page_frame_t *pages, *pages_start = NULL;
  page_frame_t *pages_ncont[MAX_PGS_NCONT];
  
  tf->printf("Target MM pool: %s\n", mmpools_get_pool_name(tlsf_ctx.pool->type));
  tf->printf("Number of allocatable pages: %d\n", tlsf_ctx.pool->free_pages);
  saved_ap = tlsf_ctx.pool->free_pages;
  tf->printf("TLSF dump:\n");
  tlsf_memdump_dbg(tlsf_ctx.tlsf);
  allocated = __allocate_all(tf, &pages_start);
  tf->printf("Allocated: %d; Available %d\n", allocated, tlsf_ctx.pool->free_pages);
  tf->printf("Now try to free all pages.\n");
  list_for_each_safe(&pages_start->head, iter, safe) {
    page_idx_t sz;

    pages = list_entry(iter, page_frame_t, node);    
    sz = pages_block_size(pages);
    free_pages(pages, sz);
    allocated -= sz;
  }

  tf->printf("Allocated: %d; Available %d\n", allocated, tlsf_ctx.pool->free_pages);
  if (allocated) {
    tf->printf("Something going wrong. After deallocation of all frames %d are rest.\n", allocated);
    tf->failed();
  }
  
  tlsf_memdump_dbg(tlsf_ctx.tlsf);
  tf->printf("Allocate all available pages again, but free them one by one.\n");
  allocated = __allocate_all(tf, &pages_start);
  tf->printf("Allocated: %d, Free: %d\n", allocated, tlsf_ctx.pool->free_pages);
  list_for_each_safe(&pages_start->head, iter, safe) {
    page_idx_t i, sz;    

    pages = list_entry(iter, page_frame_t, node);
    sz = pages_block_size(pages);
    for (i = 0; i < sz; i++, allocated--)
      free_page(pages + i);
  }

  tlsf_memdump_dbg(tlsf_ctx.tlsf);
  tf->printf("Allocated: %d, Free: %d\n", allocated, tlsf_ctx.pool->free_pages);
  num = tlsf_ctx.pool->free_pages;
  tf->printf("Allocate %d non-continous pages...\n", num);
  pages_start = alloc_pages_ncont(num, AF_CLEAR_RC);
  if (!pages_start) {
    tf->printf("Can not allocate %d non-continous pages!\n", num);
    tf->failed();
  }

  tf->printf("Validating allocated non-continous pages...\n");
  allocated = 0;
  list_for_each_entry(&pages_start->head, pages, node) {
    int sz = pages_block_size(pages);
    for (i = 0; i < sz; i++);
    allocated += i;
  }
  if (allocated != num) {
    tf->printf("FAILED! Requested(%d) != allocated(%d)\n", num, allocated);
    tf->failed();
  }

  tf->printf("Ok, now free allocated earlier non-continous pages block.\n");
  free_pages_ncont(pages_start);
  tlsf_memdump_dbg(tlsf_ctx.tlsf);
  
  tf->printf("Try to allocate all memory by non-continous memory blocks untill we get -ENOMEM\n");
  pages_start = pages = NULL;
  i = 0;
  do {
    num = __simple_random() % 1023;    
    tf->printf("Allocating: %d pages... ", num);
    pages = alloc_pages_ncont(num, AF_PGEN);
    if (!pages) {
      tf->printf("[FAILED]\n");
      break;
    }

    ASSERT(i < MAX_PGS_NCONT);
    pages_ncont[i++] = pages;
    tf->printf("[DONE]\n");
    tlsf_validate_dbg(tlsf_ctx.tlsf);
  } while (pages_start);
  
  tlsf_memdump_dbg(tlsf_ctx.tlsf);
  tf->printf("Ok, new free them all\n");
  while (i--)
    free_pages_ncont(pages_ncont[i]);
  
  tlsf_validate_dbg(tlsf_ctx.tlsf);
  tlsf_memdump_dbg(tlsf_ctx.tlsf);
  if (saved_ap != atomic_get(&tlsf_ctx.pool->free_pages)) {
    tf->printf("After all %d pages expected to be free, but only %d are actually free\n",
               saved_ap, atomic_get(&tlsf_ctx.pool->free_pages));
    tf->failed();
  }

  tlsf_ctx.completed = true;
  sys_exit(0);
}

static void tlsf_test_run(test_framework_t *tf, void *unused)
{
  if (kernel_thread(tc_alloc_dealloc, tf, NULL)) {
    tf->printf("Can't create kernel thread!");
    tf->abort();
  }

  tf->test_completion_loop(TLSF_TEST_ID, &tlsf_ctx.completed);
}

static bool tlsf_test_init(void **unused)
{
  memset(&tlsf_ctx, 0, sizeof(tlsf_ctx));
  tlsf_ctx.pool = mmpools_get_pool(POOL_GENERAL);
  if (!tlsf_ctx.pool->is_active)
    return false;
  if (tlsf_ctx.pool->allocator.type != PFA_TLSF)
    return false;

  tlsf_ctx.tlsf = tlsf_ctx.pool->allocator.alloc_ctx;
  tlsf_ctx.completed = false;
  return true;
}

static void tlsf_test_deinit(void *unused)
{
}

testcase_t tlsf_testcase = {
  .id = TLSF_TEST_ID,
  .initialize = tlsf_test_init,
  .run = tlsf_test_run,
  .deinitialize = tlsf_test_deinit,
};