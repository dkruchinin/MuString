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
 * include/mm/pfi.h: Contains definitions of all available page frame iterators.
 *
 */

#ifndef __PFI_H__
#define __PFI_H__

#include <ds/iterator.h>
#include <mm/page.h>
#include <mlibc/types.h>
#include <eza/arch/ptable.h>

/****************
 * PF_ITER_INDEX
 */

/**
 * PF_ITER_INDEX iterates through index range starting from
 * @e first and ended with @e last index.
 *
 * @see page_frame_iterator_t
 * @see DEFINE_ITERATOR_CTX
 */
DEFINE_ITERATOR_CTX(page_frame, PF_ITER_INDEX,
                    page_idx_t first;
                    page_idx_t last;);

/**
 * @brief Initialize page frame index iterator
 *
 * Index iterator browses through a range of page frame continous indices.
 * It iterates from index @a start_pfi to index @a end_pfi(inclusive).
 *
 * @param pfi[out]  - A pointer to page_frame_iterator_t
 * @param ctx[out]  - A pointer to PF_ITER_INDEX page frame iterator contex
 * @param start_pfi - Page frme index iteration starts with.
 * @param end_pfi   - Page frame index where iteration ends.
 *
 * @see page_frame_iterator_t
 * @see DEFINE_ITERATOR_CTX
 * @see DEFINE_ITERATOR_TYPES
 * @see ITERATOR_CTX
 */
void pfi_index_init(page_frame_iterator_t *pfi,
                    ITERATOR_CTX(page_frame, PF_ITER_INDEX) *ctx,
                    page_idx_t start_pfi, page_idx_t end_pfi);

/***************
 * PF_ITER_LIST
 */

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
                    list_node_t *last_node;);


/**
 * @brief Ininitialize page frame list iterator
 *
 * Iterates through list or sublist of page frames.
 *
 * @param pfi[out]       - A pointer to page_frame_t.
 * @param ctx[out]       - A pointer to PF_ITER_LIST page frame iterator context.
 * @param first_node[in] - The first list node of page frames list iteration starts with.
 * @param last_node[in]  - The last list node of page frames list iteration ends.
 *
 * @see page_frame_iterator_t
 * @see DEFINE_ITERATOR_CTX
 * @see DEFINE_ITERATOR_TYPES
 * @see ITERATOR_CTX 
 */
void pfi_list_init(page_frame_iterator_t *pfi,
                   ITERATOR_CTX(page_frame, PF_ITER_LIST) *ctx,
                   list_node_t *first_node, list_node_t *last_node);

/***************
 * PF_ITER_ARCH
 */

/**
 * @brief Initialize architecture-specific page frame iterator
 *
 * Actually this kind of interator may be used only one time during memory
 * manager initialization. Strictly speaking it is used for browsing through
 * all page frames in a system during building page frames array.
 * After it was used one time, it is automatically disabled. Attemtion to initialize
 * this iterator second time will rise kernel panic.
 *
 * @param pfi - A pointer to page frame iterator.
 * @param ctx - PF_ITER_ARCH context.
 * @see page_frame_iterator_t
 * @see DEFINE_ITERATOR_CTX
 * @see DEFINE_ITERATOR_TYPES
 * @see ITERATOR_CTX
 */
void pfi_arch_init(page_frame_iterator_t *pfi,
                   ITERATOR_CTX(page_frame, PF_ITER_ARCH) *ctx;);

struct __rpd;

void pfi_ptable_init(page_frame_iterator_t *pfi,
                     ITERATOR_CTX(page_frame, PF_ITER_PTABLE) *ctx,
                     struct __rpd *rpd, uintptr_t va_from, ulong_t npages);

DEFINE_ITERATOR_CTX(page_frame, PF_ITER_PBLOCK,
                    list_node_t *first_node;
                    list_node_t *cur_node;
                    list_node_t *last_node;
                    page_idx_t cur_idx;
                    page_idx_t first_idx;
                    page_idx_t last_idx;
                    );

void pfi_pblock_init(page_frame_iterator_t *pfi,
                     ITERATOR_CTX(page_frame, PF_ITER_PBLOCK) *ctx,
                     list_node_t *fnode, page_idx_t fidx,
                     list_node_t *lnode, page_idx_t lidx);

#endif /* __PFI_H__ */
