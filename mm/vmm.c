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
 * mm/vmm.c: Basic VMM implementation.
 *
 */

#include <ds/iterator.h>
#include <ds/list.h>
#include <mm/page.h>
#include <mm/mmpool.h>
#include <mm/pfalloc.h>
#include <mm/vmm.h>
#include <mm/idalloc.h>
#include <mm/pfi.h>
#include <mm/vmm.h>
#include <mm/memobj.h>
#include <mm/ptable.h>
#include <eza/arch/mm.h>
#include <eza/arch/ptable.h>
#include <mlibc/types.h>

mm_pool_t mm_pools[NOF_MM_POOLS];

/* initialize opne page */
static void __init_page(page_frame_t *page)
{
  list_init_head(&page->head);
  list_init_node(&page->node);
  atomic_set(&page->refcount, 0);
  page->_private = 0;
}

void mmpools_add_page(page_frame_t *page)
{
  mm_pool_t *pool = mmpools_get_pool(pool_type);

  page->flags |= ;
  if (!pool->is_activate)
    pool->is_active = true;
  if (page->flags & PF_RESERVED) {
    page->flags |= 
    pool->reserved_pages++;
    list_add2tail(&pool->reserved, &page->node);
  }
  else
    atomic_inc(&pool->free_pages);

  pool->total_pages++;
}

void mm_initialize(void)
{
  mm_pool_t *pool;
  page_frame_iterator_t pfi;
  ITERATOR_CTX(page_frame, PF_ITER_ARCH) pfi_arch_ctx;

  arch_mm_init();
  memset(mm_pools, 0, sizeof(*mm_pools) * NOF_MM_POOLS);

  /*
   * PF_ITER_ARCH page frame iterator iterates through page_frame_t 
   * structures located in the page_frames_array. It starts from the
   * very first page and iterates forward until the last page available
   * in the system is handled. On each iteration it returns an
   * index of initialized by arhitecture-dependent level page frame.
   */
  pfi_arch_init(&pfi, &pfi_arch_ctx);
  
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
  idalloc_enable(pool, CONFIG_IDALLOC_PAGES + arch_num_pages_to_reserve());
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
  if (ptable_ops.initialize_rpd(&kernel_rpd))
    panic("mm_init: Can't initialize kernel root page directory!");

  /* Now we can remap available memory */
  arch_mm_remap_pages();
  kprintf("[MM] All pages were successfully remapped.\n");
}

void vmm_initialize(void)
{
  memobj_subsystem_initialize();
  vmm_subsystem_initialize();
}

static void __pfiter_idx_first(page_frame_iterator_t *pfi)
{
  ITERATOR_CTX(page_frame, PF_ITER_INDEX) *ctx;
  
  ITER_DBG_CHECK_TYPE(pfi, F_ITER_INDEX);
  ctx = iter_fetch_ctx(pfi);
  pfi->pf_idx = ctx->first;
  pfi->state = ITER_RUN;
}

static void __pfiter_idx_last(page_frame_iterator_t *pfi)
{
  ITERATOR_CTX(page_frame, PF_ITER_INDEX) *ctx;
  
  ITER_DBG_CHECK_TYPE(pfi, PF_ITER_INDEX);  
  ctx = iter_fetch_ctx(pfi);
  pfi->pf_idx = ctx->last;
  pfi->state = ITER_RUN;
}

static void __pfiter_idx_next(page_frame_iterator_t *pfi)
{  
  ITERATOR_CTX(page_frame, PF_ITER_INDEX) *ctx;

  ITER_DBG_CHECK_TYPE(pfi, PF_ITER_INDEX);
  ctx = iter_fetch_ctx(pfi);
  if (unlikely(pfi->pf_idx > ctx->last)) {
    pfi->pf_idx = PAGE_IDX_INVAL;
    pfi->state = ITER_STOP;
    return;
  }

  pfi->pf_idx++;
}

static void __pfiter_idx_prev(page_frame_iterator_t *pfi)
{
  ITERATOR_CTX(page_frame, PF_ITER_INDEX) *ctx;

  ITER_DBG_CHECK_TYPE(pfi, PF_ITER_INDEX);
  ctx = iter_fetch_ctx(pfi);
  if (unlikely(pfi->pf_idx <= ctx->first)) {
    pfi->pf_idx = PAGE_IDX_INVAL;
    pfi->state = ITER_STOP;
  }
  else
    pfi->pf_idx--;
}

void pfi_index_init(page_frame_iterator_t *pfi,
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
  pfi->pf_idx = PAGE_IDX_INVAL;
  pfi->error = 0;
  iter_set_ctx(pfi, ctx);
}

static void __pfiter_list_first(page_frame_iterator_t *pfi)
{
  ITERATOR_CTX(page_frame, PF_ITER_LIST) *ctx;

  ITER_DBG_CHECK_TYPE(pfi, PF_ITER_LIST);
  ctx = iter_fetch_ctx(pfi);
  ctx->cur = ctx->first_node;
  pfi->pf_idx =
    pframe_number(list_entry(ctx->cur, page_frame_t, node));
  pfi->state = ITER_RUN;
}

static void __pfiter_list_last(page_frame_iterator_t *pfi)
{
  ITERATOR_CTX(page_frame, PF_ITER_LIST) *ctx;

  ITER_DBG_CHECK_TYPE(pfi, PF_ITER_LIST);
  ctx = iter_fetch_ctx(pfi);
  ctx->cur = ctx->last_node;
  pfi->pf_idx =
    pframe_number(list_entry(ctx->cur, page_frame_t, node));
  pfi->state = ITER_RUN;
}

static void __pfiter_list_next(page_frame_iterator_t *pfi)
{
  ITERATOR_CTX(page_frame, PF_ITER_LIST) *ctx;

  ITER_DBG_CHECK_TYPE(page_frame, PF_ITER_LIST);
  ctx = iter_fetch_ctx(pfi);
  if (likely(ctx->cur != ctx->last_node)) {
    ctx->cur = ctx->cur->next;
    pfi->pf_idx =
      pframe_number(list_entry(ctx->cur, page_frame_t, node));
  }
  else {
    pfi->pf_idx = PAGE_IDX_INVAL;
    pfi->state = ITER_STOP;
  }
}

static void __pfiter_list_prev(page_frame_iterator_t *pfi)
{
  ITERATOR_CTX(page_frame, PF_ITER_LIST) *ctx;

  ITER_DBG_CHECK_TYPE(pfi, PF_ITER_LIST);
  ctx = iter_fetch_ctx(pfi);
  if (likely(ctx->cur != ctx->first_node)) {
    ctx->cur = ctx->cur->prev;
    pfi->pf_idx =
      pframe_number(list_entry(ctx->cur, page_frame_t, node));
  }
  else {
    pfi->pf_idx = PAGE_IDX_INVAL;
    pfi->state = ITER_STOP;
  }
}

void pfi_list_init(page_frame_iterator_t *pfi,
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
  pfi->pf_idx = PAGE_IDX_INVAL;
  pfi->error = 0;
  iter_set_ctx(pfi, ctx);
}

static void __pfiter_pblock_first(page_frame_iterator_t *pfi)
{
  ITERATOR_CTX(page_frame, PF_ITER_PBLOCK) *ctx;

  ASSERT(pfi->type == PF_ITER_PBLOCK);
  ctx = iter_fetch_ctx(pfi);
  ctx->cur_node = ctx->first_node;
  ctx->cur_idx = ctx->first_idx;
  pfi->pf_idx =
    pframe_number(list_entry(ctx->cur_node, page_frame_t, node) + ctx->cur_idx);
  pfi->state = ITER_RUN;
}

static void __pfiter_pblock_last(page_frame_iterator_t *pfi)
{
  ITERATOR_CTX(page_frame, PF_ITER_PBLOCK) *ctx;

  ASSERT(pfi->type == PF_ITER_PBLOCK);
  ctx->cur_node = ctx->last_node;
  ctx->cur_idx = ctx->last_idx;
  pfi->pf_idx =
    pframe_number(list_entry(ctx->cur_node, page_frame_t, node) + ctx->cur_idx);
  pfi->state = ITER_RUN;
}

static void __pfiter_pblock_next(page_frame_iterator_t *pfi)
{
  ITERATOR_CTX(page_frame, PF_ITER_PBLOCK) *ctx;

  ITER_DBG_CHECK_TYPE(pfi, PF_ITER_PBLOCK);
  ctx = iter_fetch_ctx(pfi);
  ctx->cur_idx++;
  if (unlikely((ctx->cur_node == ctx->last_node) &&
               (ctx->cur_idx >= ctx->last_idx))) {
    pfi->pf_idx = PAGE_IDX_INVAL;
    pfi->state = ITER_STOP;
  }
  else {    
    if (ctx->cur_idx >= pages_block_size(list_entry(ctx->cur_node, page_frame_t, node))) {
      ctx->cur_node = ctx->cur_node->next;
      ctx->cur_idx = 0;
    }

    pfi->pf_idx =
      pframe_number(list_entry(ctx->cur_node, page_frame_t, node) + ctx->cur_idx);
  }
}

static void __pfiter_pblock_prev(page_frame_iterator_t *pfi)
{
  ITERATOR_CTX(page_frame, PF_ITER_PBLOCK) *ctx;

  ITER_DBG_CHECK_TYPE(pfi, PF_ITER_PBLOCK);
  ctx = iter_fetch_ctx(pfi);
  if (unlikely((ctx->cur_node == ctx->first_node) &&
               (ctx->cur_idx <= ctx->first_idx))) {
    pfi->pf_idx = PAGE_IDX_INVAL;
    pfi->state = ITER_STOP;
  }
  else {
    if (!ctx->cur_idx)
      ctx->cur_node = ctx->cur_node->prev;
    else
      ctx->cur_idx--;

    pfi->pf_idx =
      pframe_number(list_entry(ctx->cur_node, page_frame_t, node) + ctx->cur_idx);
  }
}

void pfi_pblock_init(page_frame_iterator_t *pfi,
                     ITERATOR_CTX(page_frame, PF_ITER_PBLOCK) *ctx,
                     list_node_t *fnode, page_idx_t fidx,
                     list_node_t *lnode, page_idx_t lidx)
{
  pfi->first = __pfiter_pblock_first;
  pfi->last = __pfiter_pblock_last;
  pfi->next = __pfiter_pblock_next;
  pfi->prev = __pfiter_pblock_prev;
  iter_init(pfi, PF_ITER_PBLOCK);
  memset(ctx, 0, sizeof(*ctx));
  ctx->first_node = fnode;
  ctx->first_idx = fidx;
  ctx->last_node = lnode;
  ctx->last_idx = lidx;
  pfi->pf_idx = PAGE_IDX_INVAL;
  pfi->error = 0;
  iter_set_ctx(pfi, ctx);
}

static void __pfi_first(page_frame_iterator_t *pfi);
static void __pfi_next(page_frame_iterator_t *pfi);

void pfi_ptable_init(page_frame_iterator_t *pfi,
                     ITERATOR_CTX(page_frame, PF_ITER_PTABLE) *ctx,
                     rpd_t *rpd, uintptr_t va_from, page_idx_t npages)
{
  pfi->first = __pfi_first;
  pfi->next = __pfi_next;
  pfi->last = pfi->prev = NULL;
  iter_init(pfi, PF_ITER_PTABLE);
  memset(ctx, 0, sizeof(*ctx));
  ctx->va_from = va_from;
  ctx->va_to = va_from + (npages << PAGE_WIDTH);
  ctx->rpd = rpd;
  pfi->pf_idx = PAGE_IDX_INVAL;
  pfi->error = 0;
  iter_set_ctx(pfi, ctx);
}

static void __pfi_first(page_frame_iterator_t *pfi)
{
  ITERATOR_CTX(page_frame, PF_ITER_PTABLE) *ctx;

  ITER_DBG_CHECK_TYPE(pfi, PF_ITER_PTABLE);
  ctx = iter_fetch_ctx(pfi);
  ctx->va_cur = ctx->va_from;
  pfi->pf_idx = ptable_ops.vaddr2page_idx(ctx->rpd, ctx->va_cur);
  if (pfi->pf_idx == PAGE_IDX_INVAL) {
    pfi->error = -EFAULT;
    pfi->state = ITER_STOP;
  }
  else {
    pfi->error = 0;
    pfi->state = ITER_RUN;
  }
}

static void __pfi_next(page_frame_iterator_t *pfi)
{
  ITERATOR_CTX(page_frame, PF_ITER_PTABLE) *ctx;

  ITER_DBG_CHECK_TYPE(pfi, PF_ITER_PTABLE);
  ctx = iter_fetch_ctx(pfi);
  if (ctx->va_cur >= ctx->va_to) {
    pfi->error = 0;
    pfi->state = ITER_STOP;
    pfi->pf_idx = PAGE_IDX_INVAL;
  }
  else {
    ctx->va_cur += PAGE_SIZE;
    pfi->pf_idx = ptable_ops.vaddr2page_idx(ctx->rpd, ctx->va_cur);
    if (pfi->pf_idx == PAGE_IDX_INVAL) {
      pfi->error = -EFAULT;
      pfi->state = ITER_STOP;
    }
  }
}

