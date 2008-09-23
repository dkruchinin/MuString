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
 * include/mm/tlsf.h: TLSF O(1) page allocator
 *
 */

#ifndef __TLSF_H__
#define __TLSF_H__

#include <ds/list.h>
#include <mm/page.h>
#include <mm/mmpool.h>
#include <mm/pfalloc.h>
#include <eza/spinlock.h>
#include <eza/arch/types.h>

#ifdef DEBUG_MM
#define TLSF_DEBUG
#endif /* DEBUG_MM */

#define TLSF_FLD_SIZE       5 /**< TLSF first level directory size */
#define TLSF_SLD_SIZE       4 /**< TLSF second level directory size */
#define TLSF_CPUCACHE_PAGES 8 /* TODO DK: implement TSLF percpu caches */
#define TLSF_FIRST_OFFSET   4 /**< TLSF first offset */


#define TLSF_SLDS_MIN 2
#define TLSF_FLDS_MAX 8
#define TLSF_FLD_BITMAP_SIZE TLSF_FLD_SIZE
#define TLSF_SLD_BITMAP_SIZE (TLSF_FLD_SIZE * TLSF_SLD_SIZE)

typedef struct __tlsf_node {
  int blocks_no;
  int max_avail_size;
  list_head_t blocks;
} tlsf_node_t;

typedef uint8_t tlsf_bitmap_t;

typedef struct __tlsf {
  struct {
    tlsf_node_t nodes[TLSF_SLD_SIZE];
    int avail_nodes;    
  } map[TLSF_FLD_SIZE];
  list_head_t *percpu_cache;  
  spinlock_t lock;  
  mm_pool_type_t owner;
  page_idx_t first_page_idx;
  page_idx_t last_page_idx;
  uint8_t slds_bitmap[TLSF_SLD_BITMAP_SIZE];
  uint8_t fld_bitmap;  
} tlsf_t;

void tlsf_alloc_init(mm_pool_t *pool);

#ifdef TLSF_DEBUG
void tlsf_memdump(void *_tlsf);
#else
#define tlsf_memdump(_tlsf)
#endif /* TLSF_DEBUG */

#endif /* __TLSF_H__ */
