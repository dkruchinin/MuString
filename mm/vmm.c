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

#ifdef CONFIG_DEBUG_MM
static inline void __check_one_frame(mm_pool_t *p, page_frame_t *pf)
{
  if (pf->pool_type == p->type) {
    kprintf("[FAILED]\n");    
    panic("Page frame #%#x doesn't included into memory pool \"%s\", but indeed that frame belongs to it!",
          pframe_number(pf), p->name);
  }
}

static void __validate_mmpools_dbg(void)
{
  mm_pool_t *p;
  page_idx_t total_pages = 0, pool_total, pool_reserved, i;
  page_frame_t *pf;

  kprintf("[DBG] Validating memory pools... ");
  for_each_mm_pool(p) {
    pool_total = pool_reserved = 0;
    /* check all pages belonging to given memory pool */
    for (i = 0; i < p->total_pages; i++, pool_total++) {
      pf = pframe_by_number(i + p->first_page_id);
      if (pf->flags & PF_RESERVED)
        pool_reserved++;
      if (pf->pool_type != p->type) {
        mm_pool_t *page_pool = get_mmpool_by_type(pf->pool_type);
        
        kprintf("[FAILED]\n");
        kprintf("aga! 1\n");
        if (!page_pool) {
          panic("Page frame #%#x belongs to memory pool \"%s\", but in a \"pool_type\" field it has "
                "unexistent pool type: %d!", i + p->first_page_id, p->name, pf->pool_type);
        }
        
        panic("Memory pool \"%s\" says that page frame #%#x belongs to it, but "
              "its owner is memory pool \"%s\" indeed!", p->name, i + p->first_page_id, page_pool->name);
      }
    }
    if (pool_total != p->total_pages) {
      kprintf("[FAILED]\n");
      kprintf("aga! 2\n");
      panic("Memory pool \"%s\" has inadequate total number of pages it owns(%d): %d was expected!",
            p->name, p->total_pages, pool_total);
    }
    if (pool_reserved != p->reserved_pages) {
      kprintf("[FAILED]\n");
      kprintf("aga! 3\n");
      panic("Memory pool \"%s\" has inadequate number of reserved pages (%d): %d was expected!",
            p->name, p->reserved_pages, pool_reserved);
    }
    if ((atomic_get(&p->free_pages) != (pool_total - pool_reserved)) &&
        (p->type != BOOTMEM_POOL_TYPE)) {
      kprintf("[FAILED]\n");
      kprintf("aga! 4\n");
      panic("Memory pool \"%s\" has inadequate number of free pages (%d): %d was expected!",
            p->name, atomic_get(&p->free_pages), pool_total - pool_reserved);
    }
    if (p->first_page_id != PAGE_IDX_INVAL) {
      if (p->first_page_id != 0)
        __check_one_frame(p, pframe_by_number(p->first_page_id - 1));
      if ((p->first_page_id + p->total_pages) < num_phys_pages)
        __check_one_frame(p, pframe_by_number(p->first_page_id + p->total_pages));
    }

    total_pages += pool_total;
  }
  if (total_pages != num_phys_pages) {
    kprintf("aga! 5\n");
    kprintf("[FAILED]\n");
    panic("Unexpected total number of pages owned by different memory pools: %d, "
          "but %d was expected!", total_pages, num_phys_pages);
  }

  kprintf("[OK]\n");
}
#else
#define __validate_mmpools_dbg()
#endif /* CONFIG_DEBUG_MM */

void mm_initialize(void)
{
  mm_pool_t *pool;
  int activated_pools = 0;
  pfalloc_flags_t old_flags;

  mmpools_initialize();
  arch_mm_init();

  mmpool_activate(POOL_BOOTMEM());
  old_flags = ptable_ops.alloc_flags;
  ptable_ops.alloc_flags = AF_BMEM | AF_ZERO;  
  if (initialize_rpd(&kernel_rpd, NULL))
    panic("mm_init: Can't initialize kernel root page directory!");

  /* Now we can remap available memory */
  arch_mm_remap_pages();
  ptable_ops.alloc_flags = old_flags;
  for_each_mm_pool(pool) {
    if (!atomic_get(&pool->free_pages) || (pool->type == BOOTMEM_POOL_TYPE))
      continue;

    kprintf("activate pool %d %s\n", pool->type, pool->name);
    mmpool_activate(pool);
    activated_pools++;
  }
  if (!activated_pools)
    panic("No one memory pool was activated!");
  
  for_each_active_mm_pool(pool) {
    kprintf("[MM] Pages statistics of pool \"%s\":\n", pool->name);
    kprintf(" | %-8s %-8s %-8s |\n", "Total", "Free", "Reserved");
    kprintf(" | %-8d %-8d %-8d |\n", pool->total_pages,
            atomic_get(&pool->free_pages), pool->reserved_pages);
  }
  
  kprintf("[MM] All pages were successfully remapped.\n");
  __validate_mmpools_dbg();
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
    pframe_number(list_entry(ctx->cur, page_frame_t, chain_node));
  pfi->state = ITER_RUN;
}

static void __pfiter_list_last(page_frame_iterator_t *pfi)
{
  ITERATOR_CTX(page_frame, PF_ITER_LIST) *ctx;

  ITER_DBG_CHECK_TYPE(pfi, PF_ITER_LIST);
  ctx = iter_fetch_ctx(pfi);
  ctx->cur = ctx->last_node;
  pfi->pf_idx =
    pframe_number(list_entry(ctx->cur, page_frame_t, chain_node));
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
      pframe_number(list_entry(ctx->cur, page_frame_t, chain_node));
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
      pframe_number(list_entry(ctx->cur, page_frame_t, chain_node));
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
  pfi->pf_idx = ptable_ops.vaddr2page_idx(ctx->rpd, ctx->va_cur, NULL);
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
    pfi->pf_idx = ptable_ops.vaddr2page_idx(ctx->rpd, ctx->va_cur, NULL);
    if (pfi->pf_idx == PAGE_IDX_INVAL) {
      pfi->error = -EFAULT;
      pfi->state = ITER_STOP;
    }
  }
}

