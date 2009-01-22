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
 * include/mm/page.h: Architecture independent page-frame primitives
 *
 */

/**
 * @file include/mm/page.h
 * Architecture independent page-frame primitives
 * @author Dan Kruchinin
 */

#ifndef __PAGE_H__
#define __PAGE_H__

#include <ds/iterator.h>
#include <ds/list.h>
#include <mlibc/stddef.h>
#include <mlibc/types.h>
#include <eza/arch/page.h>
#include <eza/arch/atomic.h>

#define PAGE_ALIGN(addr) (((addr) + PAGE_MASK) & ~PAGE_MASK)
#define PAGE_ALIGN_DOWN(addr) ((addr) & ~PAGE_MASK)

#define NOF_MM_POOLS 2 /**< Number of MM pools in system */

/**
 * @typedef int page_idx_t
 * Page index.
 */
typedef int page_idx_t;

#define PAGE_IDX_INVAL (~0U)

/**
 * @typedef uint16_t page_flags_t;
 * Page flags.
 */
typedef uint16_t page_flags_t;

#define PF_PDMA       0x01 /**< DMA pool is page owner */
#define PF_PGEN       0x02 /**< GENERAL pool is page owner */
#define PF_RESERVED   0x04 /**< Page is reserved */
#define PF_SLAB_LOCK  0x08 /**< Page lock (used by slab allocator) */

#define __pool_type(flags) ((flags) >> 1)
#define PAGE_POOLS_MASK (PF_PGEN | PF_PDMA)
#define PAGE_PRESENT_MASK (PAGE_POOL_MASK | PF_RESERVED)

#define __page_aligned__ __attribute__((__aligned__(PAGE_SIZE)))

/**
 * @struct page_frame_t
 * @brief Describes one physical page.
 *
 * Each physical page has corresponding unique page_frame_t
 * item, which describes internal page attributes.
 *
 * @see pframe_pages_array
 */
typedef struct __page_frame {
  list_head_t head;    /**< Obvious */
  list_node_t node;    /**< Obvious */
  page_idx_t idx;      /**< Page frame index in the pframe_pages_array */
  atomic_t refcount;   /**< Number of references to the physical page */
  page_flags_t flags;  /**< Page flags */
  uint32_t _private;   /**< Private data that may be used by internal page frame allocator */  
} page_frame_t;

extern page_frame_t *page_frames_array; /**< An array of all available physical pages */
extern page_idx_t num_phys_pages;       /**< Number of physical pages in system */

#define pframe_pool_type(page) (__pool_type((page)->flags & PAGE_POOLS_MASK))

/**
 * @struct page_frame_iterator_t
 * Page frame iterator
 * @see DEFINE_ITERATOR
 */
DEFINE_ITERATOR(page_frame,
                int error;
                page_idx_t pf_idx;);

/**
 * Page frame iterator supported types
 * @see DEFINE_ITERATOR_TYPES
 */
DEFINE_ITERATOR_TYPES(page_frame,
                      PF_ITER_ARCH,   /**< Architecture-dependent iterator used for page frames initialization */
                      PF_ITER_INDEX,  /**< Index-based iterator */
                      PF_ITER_LIST,   /**< List-based iterator */
                      PF_ITER_ALLOC,  /**< Each next item of ALLOC iterator is dynamically allocated */
                      PF_ITER_PTABLE, /**< Page table iterator */
                      PF_ITER_PBLOCK, /**< Iterate through list of page blocks */
                      );

static inline bool page_idx_is_present(page_idx_t page_idx)
{
  return (page_idx < num_phys_pages);
}

static inline void *pframe_to_virt(page_frame_t *frame)
{
  return (void *)(KERNEL_BASE + (frame->idx << PAGE_WIDTH));
}

static inline page_idx_t pframe_number(page_frame_t *frame)
{
  return frame->idx;
}

static inline page_frame_t *pframe_by_number(page_idx_t idx)
{
  return (page_frames_array + idx);
}

static inline void *pframe_phys_addr(page_frame_t *frame)
{
  return (void *)((uintptr_t)frame->idx << PAGE_WIDTH);
}

static inline void *pframe_id_to_virt( page_idx_t idx )
{
  return (void *)(KERNEL_BASE + (idx << PAGE_WIDTH));
}

static inline page_idx_t virt_to_pframe_id(void *virt)
{
  return ((uintptr_t)virt - KERNEL_BASE) >> PAGE_WIDTH;
}

static inline page_frame_t *virt_to_pframe( void *addr )
{
  return pframe_by_number(virt_to_pframe_id(addr));
}

/**
 * @fn static inline void pframe_memnull(page_frame_t *start, int block_size)
 * @brief Fill block of @a block_size continuos pages with zero's
 * @param[out] start - A pointer to very first page frame in the block
 * @param block_size - Number of pages in block
 */
#ifndef ARCH_PAGE_MEMNULL
static inline void pframe_memnull(page_frame_t *start, int block_size)
{
  memset(pframe_to_virt(start), 0, PAGE_SIZE * block_size);
}
#else
#define pframe_memnull(start, block_size) arch_page_memnull(start, block_size)
#endif /* ARCH_PAGE_MEMNULL */
#endif /* __PAGE_H__ */
