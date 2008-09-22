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
 * mm/pageaccs.c: Common page accessors live here.
 */

#include <mlibc/stddef.h>
#include <mm/pt.h>
#include <eza/pageaccs.h>
#include <mm/pagealloc.h>
#include <eza/swks.h>

void pageaccs_reset_stub(void *ctx)
{
}

page_frame_t *pageaccs_alloc_page_stub(void *ctx,page_flags_t flags,int clean_page)
{
  return alloc_page(flags,clean_page); 
}

page_idx_t pageaccs_frames_left_stub(void *ctx)
{
  return swks.mem_total_pages;
}

/* Linear area page accessor that uses 'pageaccs_linear_page_accessor_ctx_t'
 * as its context. */
static page_idx_t linear_pa_frames_left(void *ctx)
{
  pageaccs_linear_pa_ctx_t *lctx = (pageaccs_linear_pa_ctx_t*)ctx;
  if( lctx->curr_page < lctx->end_page ) {
    return lctx->curr_page - lctx->end_page;
  } else {
    return 0;
  }
}

static page_idx_t linear_pa_next_frame(void *ctx)
{
  pageaccs_linear_pa_ctx_t *lctx = (pageaccs_linear_pa_ctx_t*)ctx;
  return lctx->curr_page++;
}

static void linear_pa_reset(void *ctx)
{
  pageaccs_linear_pa_ctx_t *lctx = (pageaccs_linear_pa_ctx_t*)ctx;
  lctx->curr_page = lctx->start_page;
}

page_frame_accessor_t pageaccs_linear_pa = {
  .frames_left = linear_pa_frames_left,
  .next_frame = linear_pa_next_frame,
  .reset = linear_pa_reset,
  .alloc_page = pageaccs_alloc_page_stub,
};


/* List-based area page accessor that uses 'pageaccs_list_page_accessor_ctx_t'
 * as its context. */
static page_idx_t list_pa_frames_left(void *ctx)
{
  pageaccs_list_pa_ctx_t *lctx = (pageaccs_list_pa_ctx_t*)ctx;
  return lctx->pages_left;
}

static page_idx_t list_pa_next_frame(void *ctx)
{
  pageaccs_list_pa_ctx_t *lctx = (pageaccs_list_pa_ctx_t*)ctx;

  if( lctx->pages_left != 0 ) {
    page_frame_t *f = container_of(lctx->curr,page_frame_t,node);

    lctx->curr = lctx->curr->next;
    lctx->pages_left--;
    return f->idx;
  } else {
    return INVALID_PAGE_IDX;
  }
}

static void list_pa_reset(void *ctx)
{
  pageaccs_list_pa_ctx_t *lctx = (pageaccs_list_pa_ctx_t*)ctx;

  //lctx->curr = list_node_first(&lctx->head->active_list);
  lctx->pages_left = lctx->num_pages;
}


page_frame_accessor_t pageaccs_list_pa = {
  .frames_left = list_pa_frames_left,
  .next_frame = list_pa_next_frame,
  .reset = list_pa_reset,
  .alloc_page = pageaccs_alloc_page_stub,
};

