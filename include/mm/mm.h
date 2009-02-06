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
 * (c) Copyright 2008 Michael Tsymbalyuk <mtzaurus@gmail.com>
 * (c) Copyright 2008 Dan Kruchinin <dan.kruchinin@gmai.com>
 *
 * include/mm/mm.h: Contains types and prototypes for the kernel memory manager.
 *
 */

/**
 * @file include/mm/mm.h
 * Architecture-independent memory manager API.
 */

#ifndef __MM_H__
#define __MM_H__

#include <ds/iterator.h>
#include <mm/page.h>
#include <eza/arch/types.h>

/**
 * PF_ITER_INDEX iterates through index range starting from
 * @e first and ended with @e last index.
 *
 * @see page_frame_iterator_t
 * @see DEFINE_ITERATOR_CTX
 */
DEFINE_ITERATOR_CTX(page_frame, PF_ITER_INDEX,
                    page_idx_t first;
                    page_idx_t last);

/**
 * PF_ITER_LIST iterates through linked list of page frames.
 * It starts from @e first_node and ends with @e last_node.
 *
 * @see page_frame_iterator_t
 * @see DEFINE_ITERATOR_CTX
 */
DEFINE_ITERATOR_CTX(page_frame, PF_ITER_LIST,
                    list_node_t *first_node;
                    list_node_t *cur;
                    list_node_t *last_node);

extern page_frame_t *page_frames_array; /**< An array of all available physical pages */
extern uintptr_t kernel_min_vaddr; /**< The bottom address of kernel virtual memory space. */

/**
 * @brief Initialize mm internals
 * @note It's an initcall, so it should be called only once during system boot stage.
 */
void mm_init(void);

/**
 * @brief Initialize page frame index iterator
 * @param pfi[out]  - A pointer to page_frame_iterator_t
 * @param ctx[out]  - A pointer to PF_ITER_INDEX page frame iterator contex
 * @param start_pfi - Page frme index iteration starts with.
 * @param end_pfi   - Page frame index iteration ends with.
 *
 * @see page_frame_iterator_t
 * @see DEFINE_ITERATOR_CTX
 * @see DEFINE_ITERATOR_TYPES
 * @see ITERATOR_CTX
 */
void mm_init_pfiter_index(page_frame_iterator_t *pfi,
                          ITERATOR_CTX(page_frame, PF_ITER_INDEX) *ctx,
                          page_idx_t start_pfi, page_idx_t end_pfi);

/**
 * @brief Ininitialize page frame list iterator
 * @param pfi[out]       - A pointer to page_frame_t.
 * @param ctx[out]       - A pointer to PF_ITER_LIST page frame iterator context.
 * @param first_node[in] - The first list node of page frames list iteration starts with.
 * @param last_node[in]  - The last list node of page frames list iteration ends with.
 *
 * @see page_frame_iterator_t
 * @see DEFINE_ITERATOR_CTX
 * @see DEFINE_ITERATOR_TYPES
 * @see ITERATOR_CTX 
 */
void mm_init_pfiter_list(page_frame_iterator_t *pfi,
                         ITERATOR_CTX(page_frame, PF_ITER_LIST) *ctx,
                         list_node_t *first_node, list_node_t *last_node);

#endif /* __MM_H__ */

