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

#include <config.h>
#include <ds/iterator.h>
#include <ds/list.h>
#include <mlibc/stddef.h>
#include <mlibc/types.h>
#include <eza/arch/page.h>
#include <eza/arch/atomic.h>

#define PAGE_ALIGN(addr) (((uintptr_t)(addr) + PAGE_MASK) & ~PAGE_MASK)
#define PAGE_ALIGN_DOWN(addr) ((uintptr_t)(addr) & ~PAGE_MASK)

/**
 * @typedef int page_idx_t
 * Page index.
 */
typedef ulong_t page_idx_t;

#define PAGE_IDX_INVAL (~0U)

/**
 * @typedef uint16_t page_flags_t;
 * Page flags.
 */
typedef uint8_t page_flags_t;

#define PF_RESERVED   0x01 /**< Page is reserved */
#define PF_SLAB_LOCK  0x02 /**< Page lock (used by slab allocator) */
#define PF_SYNCING    0x04

#define PF_MMPOOL_MASK (PF_MMP_BMEM | PF_MMP_GEN | PF_MMP_DMA)

/* page fault flags */
#define PFLT_NOT_PRESENT 0x01
#define PFLT_PROTECT     0x02
#define PFLT_READ        0x04
#define PFLT_WRITE       0x08

#define __page_aligned__ __attribute__((__aligned__(PAGE_SIZE)))

struct __memobj;

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
  list_node_t node;
  
  union {
    atomic_t refcount;
    void *slab_pages_start;
    struct __memobj *owner;
  };
  
  union {
    list_node_t chain_node;
    struct {
      pgoff_t offset;
      atomic_t dirtycount;
        
    };
  };

  page_idx_t idx;
  ulong_t _private;
  page_flags_t flags;
  uint8_t pool_type;
} page_frame_t;

extern page_frame_t *page_frames_array; /**< An array of all available physical pages */
extern page_idx_t num_phys_pages;       /**< Number of physical pages in system */

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
                      PF_ITER_INDEX,  /**< Index-based iterator */
                      PF_ITER_LIST,   /**< List-based iterator */
                      PF_ITER_PTABLE, /**< Page table iterator */
                      );

static inline bool page_idx_is_present(page_idx_t page_idx)
{
  return (page_idx <= num_phys_pages);
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

#define pframe_memnull(page) pframes_memnull(page, 1)

/**
 * @fn static inline void pframe_memnull(page_frame_t *start, int block_size)
 * @brief Fill block of @a block_size continuos pages with zero's
 * @param[out] start - A pointer to very first page frame in the block
 * @param block_size - Number of pages in block
 */
#ifndef ARCH_PAGE_MEMNULL
static inline void pframes_memnull(page_frame_t *start, int block_size)
{
  memset(pframe_to_virt(start), 0, PAGE_SIZE * block_size);
}
#else
#define pframes_memnull(start, block_size) arch_pages_memnull(start, block_size)
#endif /* ARCH_PAGE_MEMNULL */
#endif /* __PAGE_H__ */
