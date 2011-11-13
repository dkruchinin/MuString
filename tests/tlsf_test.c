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
 * (c) Copyright 2008 Dan Kruchinin <dan.kruchinin@gmail.com>
 *
 * tests/tlsf_test.c: TLSF O(1) page allocator main test
 *
 */

#include <config.h>
#include <test.h>
#include <ds/list.h>
#include <mm/page.h>
#include <mm/page_alloc.h>
#include <mm/mmpool.h>
#include <mm/vmm.h>
#include <mm/tlsf.h>
#include <mstring/task.h>
#include <kernel/syscalls.h>
#include <mstring/types.h>

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


#define MAX_PGS_NCONT 128

/* TODO DK: check automatically if all merges were done properly! */
static void tc_alloc_dealloc(void *ctx)
{
  test_framework_t *tf = ctx;  
  list_node_t *iter;
  list_head_t head;
  page_idx_t allocated, saved_ap, resr = 0;
  page_frame_t *pages;
  page_idx_t c;

  tf->printf("Target MM pool: %s\n", tlsf_ctx.pool->name);
  tf->printf("Number of allocatable pages: %d\n", tlsf_ctx.pool->free_pages);
  saved_ap = atomic_get(&tlsf_ctx.pool->free_pages);

#ifdef CONFIG_SMP
  for_each_cpu(c) {
    if (!tlsf_ctx.tlsf->percpu[c] || !c)
      continue;

    resr += tlsf_ctx.tlsf->percpu[c]->noc_pages;
  }
#endif /* CONFIG_SMP */

  tf->printf("Allocate all possible pages one-by-one...\n");
  list_init_head(&head);
  allocated = 0;
  for (;;) {
    pages = alloc_page(AF_ZERO);
    tlsf_validate_dbg(tlsf_ctx.tlsf);
    if (!pages)
      break;

    list_add2tail(&head, &pages->chain_node);
    allocated++;    
  }
  if (atomic_get(&tlsf_ctx.pool->free_pages) != resr) {
    tf->printf("Failed to allocate %d pages. %d pages rest\n",
               saved_ap, atomic_get(&tlsf_ctx.pool->free_pages));
    tf->failed();
  }
  if (allocated != saved_ap) {
    tf->printf("Not all pages was allocated from TLSF.\n");
    tf->printf("Total: %d. Allocated: %d\n", saved_ap, allocated);
  }
  
  mmpool_allocator_dump(tlsf_ctx.pool);
  tf->printf("Free allocated %d pages.\n", allocated);
  pages = list_entry(list_node_first(&head), page_frame_t, chain_node);
  list_cut_head(&head);
  free_pages_chain(pages);
  if (atomic_get(&tlsf_ctx.pool->free_pages) != saved_ap) {
    tf->printf("Not all pages were fried: %d rest (%d total)\n",
               saved_ap - atomic_get(&tlsf_ctx.pool->free_pages), saved_ap);
    tf->failed();
  }

  mmpool_allocator_dump(tlsf_ctx.pool);
  tf->printf("Allocate all possible pages usign non-continous allocation\n");
  pages = alloc_pages(saved_ap - resr, AF_ZERO | AF_USER);
  if (!pages) {
    tf->printf("Failed to allocate non-continous %d pages!\n", saved_ap);
    tf->failed();
  }

  tlsf_validate_dbg(tlsf_ctx.tlsf);
  mmpool_allocator_dump(tlsf_ctx.pool);
  allocated = 0;
  list_set_head(&head, &pages->chain_node);
  list_for_each(&head, iter)
    allocated++;
  if (allocated != (saved_ap - resr)) {
    tf->printf("Invalid number of pages allocated: %d (%d was expected)\n", allocated, saved_ap - resr);
    tf->failed();
  }

  list_cut_head(&head);
  free_pages_chain(pages);
  if (atomic_get(&tlsf_ctx.pool->free_pages) != saved_ap) {
    tf->printf("Not all pages were fried: %d rest (%d total)\n",
               saved_ap - atomic_get(&tlsf_ctx.pool->free_pages), saved_ap);
    tf->failed();
  }

  mmpool_allocator_dump(tlsf_ctx.pool);
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
  tlsf_ctx.pool = POOL_GENERAL();
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
