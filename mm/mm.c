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
 * (c) Copyright 2008 Dan Kruchinin <dan.kruchinin@gmail.com>
 *
 * mm/mm.c: Contains implementation of kernel memory manager.
 *
 */

/**
 * @file mm/mm.c
 * Basic memory management functionality.
 */

#include <config.h>
#include <ds/iterator.h>
#include <ds/list.h>
#include <mlibc/kprintf.h>
#include <mm/mm.h>
#include <mm/page.h>
#include <mm/mmpool.h>
#include <mm/idalloc.h>
#include <mm/mmap.h>
#include <eza/kernel.h>
#include <eza/arch/mm.h>
#include <eza/arch/ptable.h>

#ifndef IDALLOC_VPAGES
#define IDALLOC_VPAGES 64
#endif

/* An array of all physical pages */
page_frame_t *page_frames_array;

/* initialize opne page */
static void __init_page(page_frame_t *page)
{
  list_init_head(&page->head);
  list_init_node(&page->node);
  atomic_set(&page->refcount, 0);
  page->_private = 0;
}

void mm_init(void)
{
  mm_pool_t *pool;
  page_frame_iterator_t pfi;
  ITERATOR_CTX(page_frame, PF_ITER_ARCH) pfi_arch_ctx;

  arch_mm_init();
  mmpools_init();

  /*
   * PF_ITER_ARCH page frame iterator iterates through page_frame_t 
   * structures located in the page_frames_array. It starts from the
   * very first page and iterates forward until the last page available
   * in the system is handled. On each iteration it returns an
   * index of initialized by arhitecture-dependent level page frame.
   */  
  arch_mm_page_iter_init(&pfi, &pfi_arch_ctx);
  
  /* initialize page and add it to the related pool */
  iterate_forward(&pfi) {
    page_frame_t *page = pframe_by_number(pfi.pf_idx);
    __init_page(page);
    mmpools_add_page(page);
  }

  kprintf("[MM] Memory pools were initialized\n");
  
  /*
   * Now we may initialize "init data allocator"
   * Note: idalloc allocator will cut from general pool's
   * pages no more than CONFIG_IDALLOC_PAGES. After initialization
   * is done, idalloc must be explicitely disabled.
   */
  pool = mmpools_get_pool(POOL_GENERAL);
  ASSERT(pool->free_pages);
  idalloc_enable(pool);
  kprintf("[MM] Init-data memory allocator was initialized.\n");
  kprintf(" idalloc available pages: %ld\n", idalloc_meminfo.npages);  
  for_each_active_mm_pool(pool) {
    char *name = mmpools_get_pool_name(pool->type);
    
    kprintf("[MM] Pages statistics of pool \"%s\":\n", name);
    kprintf(" | %-8s %-8s %-8s |\n", "Total", "Free", "Reserved");
    kprintf(" | %-8d %-8d %-8d |\n", pool->total_pages,
            atomic_get(&pool->free_pages), pool->reserved_pages);
    mmpools_init_pool_allocator(pool);
  }

  /* create and initialize kernel pagatable root directory */
  kernel_root_pagedir = mm_create_root_pagedir();
  if (!kernel_root_pagedir)
      panic("Can't create kernel root page directory (ENOMEM)");

  /* Now we can remap memory */
  arch_mm_remap_pages();

  /* After all memory has been remapped, we can reserve some space
   * for initial virtual memory range allocation.
   */
  idalloc_meminfo.num_vpages=IDALLOC_VPAGES;
  idalloc_meminfo.avail_vpages=IDALLOC_VPAGES;
  idalloc_meminfo.virt_top=kernel_min_vaddr;
  kernel_min_vaddr-=IDALLOC_VPAGES*PAGE_SIZE;

  kprintf("[MM] All pages were successfully remapped\n");
}

static void __pfiter_idx_first(page_frame_iterator_t *pfi)
{
  ITERATOR_CTX(page_frame, PF_ITER_INDEX) *ctx;

  ASSERT(pfi->type == PF_ITER_INDEX);
  ctx = iter_fetch_ctx(pfi);
  pfi->pf_idx = ctx->first;
  pfi->state = ITER_RUN;
}

static void __pfiter_idx_last(page_frame_iterator_t *pfi)
{
  ITERATOR_CTX(page_frame, PF_ITER_INDEX) *ctx;

  ASSERT(pfi->type == PF_ITER_INDEX);
  ctx = iter_fetch_ctx(pfi);
  pfi->pf_idx = ctx->last;
  pfi->state = ITER_RUN;
}

static void __pfiter_idx_next(page_frame_iterator_t *pfi)
{  
  ASSERT(pfi->type == PF_ITER_INDEX);    
  if (pfi->pf_idx == PF_ITER_UNDEF_VAL)
    iter_first(pfi);
  else {
    ITERATOR_CTX(page_frame, PF_ITER_INDEX) *ctx;

    ctx = iter_fetch_ctx(pfi);
    if (pfi->pf_idx > ctx->last) {
      pfi->state = ITER_STOP;
      pfi->pf_idx = PF_ITER_UNDEF_VAL;
      return;
    }

    pfi->pf_idx++;
  }
}

static void __pfiter_idx_prev(page_frame_iterator_t *pfi)
{  
  ASSERT(pfi->type == PF_ITER_INDEX);  
  if (pfi->pf_idx == PF_ITER_UNDEF_VAL)
    iter_last(pfi);
  else {
    ITERATOR_CTX(page_frame, PF_ITER_INDEX) *ctx;

    ctx = iter_fetch_ctx(pfi);
    if (pfi->pf_idx < ctx->first) {
      pfi->state = ITER_STOP;
      pfi->pf_idx = PF_ITER_UNDEF_VAL;
      return;
    }

    pfi->pf_idx--;
  }
}

void mm_init_pfiter_index(page_frame_iterator_t *pfi,
                          ITERATOR_CTX(page_frame, PF_ITER_INDEX) *ctx,
                          page_idx_t start_pfi, page_idx_t end_pfi)
{
  pfi->first = __pfiter_idx_first;
  pfi->last = __pfiter_idx_last;
  pfi->next = __pfiter_idx_next;
  pfi->prev = __pfiter_idx_prev;
  iter_init(pfi, PF_ITER_INDEX);
  memset(ctx, 0, sizeof(*ctx));
  ctx->first = start_pfi;
  ctx->last = end_pfi;
  pfi->pf_idx = PF_ITER_UNDEF_VAL;
  iter_set_ctx(pfi, ctx);
}

static void __pfiter_list_first(page_frame_iterator_t *pfi)
{
  ITERATOR_CTX(page_frame, PF_ITER_LIST) *ctx;

  ASSERT(pfi->type == PF_ITER_LIST);
  ctx = iter_fetch_ctx(pfi);
  ctx->cur = ctx->first_node;
  pfi->pf_idx =
    pframe_number(list_entry(ctx->cur, page_frame_t, node));
  pfi->state = ITER_RUN;
}

static void __pfiter_list_last(page_frame_iterator_t *pfi)
{
  ITERATOR_CTX(page_frame, PF_ITER_LIST) *ctx;

  ASSERT(pfi->type == PF_ITER_LIST);
  ctx = iter_fetch_ctx(pfi);
  ctx->cur = ctx->last_node;
  pfi->pf_idx =
    pframe_number(list_entry(ctx->cur, page_frame_t, node));
  pfi->state = ITER_RUN;
}

static void __pfiter_list_next(page_frame_iterator_t *pfi)
{
  ASSERT(pfi->type == PF_ITER_LIST);
  if (pfi->pf_idx == PF_ITER_UNDEF_VAL)
    iter_first(pfi);
  else {
    ITERATOR_CTX(page_frame, PF_ITER_LIST) *ctx;

    ctx = iter_fetch_ctx(pfi);
    if (ctx->cur == ctx->last_node) {
      pfi->state = ITER_STOP;
      pfi->pf_idx = PF_ITER_UNDEF_VAL;
      return;
    }

    ctx->cur = ctx->cur->next;
    pfi->pf_idx = pframe_number(list_entry(ctx->cur, page_frame_t, node));
  }
}

static void __pfiter_list_prev(page_frame_iterator_t *pfi)
{
  ASSERT(pfi->type == PF_ITER_LIST);
  if (pfi->pf_idx == PF_ITER_UNDEF_VAL)
    iter_last(pfi);
  else {
    ITERATOR_CTX(page_frame, PF_ITER_LIST) *ctx;

    ctx = iter_fetch_ctx(pfi);
    if (ctx->cur == ctx->first_node) {
      pfi->state = ITER_STOP;
      pfi->pf_idx = PF_ITER_UNDEF_VAL;
    }

    ctx->cur = ctx->cur->prev;
    pfi->pf_idx = pframe_number(list_entry(ctx->cur, page_frame_t, node));
  }
}

void mm_init_pfiter_list(page_frame_iterator_t *pfi,
                         ITERATOR_CTX(page_frame, PF_ITER_LIST) *ctx,
                         list_node_t *first_node, list_node_t *last_node)
{
  pfi->first = __pfiter_list_first;
  pfi->last = __pfiter_list_last;
  pfi->next = __pfiter_list_next;
  pfi->prev = __pfiter_list_prev;
  iter_init(pfi, PF_ITER_LIST);
  memset(ctx, 0, sizeof(*ctx));
  ctx->first_node = first_node;
  ctx->last_node = last_node;
  pfi->pf_idx = PF_ITER_UNDEF_VAL;
  iter_set_ctx(pfi, ctx);
}
