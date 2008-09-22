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
 * (c) Copyright 2008 Dan Kruchinin <dan.kruchinin@gmai.com>
 *
 * mm/mm.c: Contains types and prototypes for the kernel memory manager.
 *
 */


#ifndef __MM_H__
#define __MM_H__

#include <ds/iterator.h>
#include <mm/page.h>
#include <eza/arch/types.h>

DEFINE_ITERATOR_CTX(page_frame, PF_ITER_INDEX,
                    page_idx_t first;
                    page_idx_t last);

DEFINE_ITERATOR_CTX(page_frame, PF_ITER_LIST,
                    list_node_t *first_node;
                    list_node_t *cur;
                    list_node_t *last_node);

/* page frames detected in the system */
extern page_frame_t *page_frames_array;

void mm_init(void);
void mm_init_pfiter_index(page_frame_iterator_t *pfi,
                          ITERATOR_CTX(page_frame, PF_ITER_INDEX) *ctx,
                          page_idx_t start_pfi, page_idx_t end_pfi);
void mm_init_pfiter_list(page_frame_iterator_t *pfi,
                         ITERATOR_CTX(page_frame, PF_ITER_LIST) *ctx,
                         list_node_t *first_node, list_node_t *last_node);

#endif /* __MM_H__ */

