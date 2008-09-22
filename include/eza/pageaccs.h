
#ifndef __PAGEACCS_H__
#define __PAGEACCS_H__ 

#include <eza/arch/types.h>
#include <mm/mm.h>
#include <ds/list.h>

/* Context for linear area page accessor. */
typedef struct __pageaccs_linear_pa_ctx {
  page_idx_t start_page, end_page, curr_page;
} pageaccs_linear_pa_ctx_t;

/* Linear area page accessor that uses 'pageaccs_linear_pa_ctx_t'
 * as its context. */
extern page_frame_accessor_t pageaccs_linear_pa;

/* Page accessor for non-contiguous physical pages. Uses a list of
 * pageframes to map.
 */
typedef struct __pageaccs_list_pa_ctx {
  list_head_t *head;
  list_node_t *curr;
  page_idx_t num_pages, pages_left;
} pageaccs_list_pa_ctx_t;

/* List-based area page accessor that uses 'pageaccs_list_pa_ctx_t'
 * as its context. */
extern page_frame_accessor_t pageaccs_list_pa;

/* Stubs for simple page accesses. */
void pageaccs_reset_stub(void *ctx);
page_frame_t *pageaccs_alloc_page_stub(void *ctx,page_flags_t flags,int clean_page);
page_idx_t pageaccs_frames_left_stub(void *ctx);


#endif

