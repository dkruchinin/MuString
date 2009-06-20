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
#include <ds/list.h>
#include <mstring/stddef.h>
#include <mstring/types.h>
#include <arch/page.h>
#include <arch/atomic.h>

#define PAGE_ALIGN(addr) (((uintptr_t)(addr) + PAGE_MASK) & ~PAGE_MASK)
#define PAGE_ALIGN_DOWN(addr) ((uintptr_t)(addr) & ~PAGE_MASK)

#define MMPOOLS_MASK    0xf
#define MMPOOLS_SHIFT   4
#define MMPOOLS_MAX     (1 << MMPOOLS_SHIFT)
#define PF_MMPOOL(type) ((type) & MMPOOLS_MASK)

/**
 * @typedef int page_idx_t
 * Page index.
 */
typedef ulong_t page_idx_t;

#define PAGE_IDX_INVAL (~0UL)

/**
 * @typedef uint16_t page_flags_t;
 * Page flags.
 */
typedef uint16_t page_flags_t;

#define PF_RESERVED   (0x01 << MMPOOLS_SHIFT)
#define PF_DIRTY      (0x02 << MMPOOLS_SHIFT)
#define PF_COW        (0x04 << MMPOOLS_SHIFT)
#define PF_SHARED     (0x08 << MMPOOLS_SHIFT)
#define PF_SLAB       (0x10 << MMPOOLS_SHIFT)
#define PF_PENDING    (0x20 << MMPOOLS_SHIFT)
#define PF_LOCK       (0x40 << MMPOOLS_SHIFT)

#define PF_CLEAR_MASK (PF_COW | PF_DIRTY | PF_SHARED | PF_SLAB | PF_PENDING)

/* page fault flags */
#define PFLT_NOT_PRESENT 0x01
#define PFLT_PROTECT     0x02
#define PFLT_READ        0x04
#define PFLT_WRITE       0x08
#define PFLT_NOEXEC      0x10

#define __page_aligned__ __attribute__((__aligned__(PAGE_SIZE)))

struct __rmap_group_head;
struct __rmap_group_entry;

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
  list_node_t chain_node;

  union {
    void *slab_ptr;
    atomic_t refcount;
  };
  union {
    struct __rmap_group_head *rmap_shared;
    struct __rmap_group_entry *rmap_anon;
    void *slab_lazy_freelist;
  };
  union {
    pgoff_t offset;
    int slab_num_lazy_objs;
  };

  page_idx_t idx;
  ulong_t _private;
  page_flags_t flags;
  uint8_t pool_type;
} page_frame_t;

extern page_frame_t *page_frames_array; /**< An array of all available physical pages */
extern page_idx_t num_phys_pages;       /**< Number of physical pages in system */

static inline bool page_idx_is_present(page_idx_t page_idx)
{
  return (page_idx <= num_phys_pages);
}

static inline page_idx_t pframe_number(page_frame_t *page)
{
  return (page_idx_t)(page - page_frames_array);
}

static inline page_frame_t *pframe_by_id(page_idx_t page_idx)
{
  return (page_frames_array + page_idx);
}

static inline void *pframe_id_to_virt(page_idx_t page_idx)
{
  return (void *)PHYS_TO_KVIRT((uintptr_t)page_idx << PAGE_WIDTH);
}

static inline void *pframe_id_to_phys(page_idx_t page_idx)
{
  return (void *)((uintptr_t)page_idx << PAGE_WIDTH);
}

static inline page_idx_t virt_to_pframe_id(void *virt)
{
  return (KVIRT_TO_PHYS(virt) >> PAGE_WIDTH);
}

static inline page_idx_t phys_to_pframe_id(void *phys)
{
  return ((uintptr_t)phys >> PAGE_WIDTH);
}

static inline void *pframe_to_virt(page_frame_t *page)
{
  return (void *)PHYS_TO_KVIRT((uintptr_t)(page - page_frames_array) << PAGE_WIDTH);
}

static inline void *pframe_to_phys(page_frame_t *page)
{
  return (void *)((uintptr_t)(page - page_frames_array) << PAGE_WIDTH);
}

static inline page_frame_t *virt_to_pframe(void *virt)
{
  return (page_frames_array + virt_to_pframe_id(virt));
}

static inline page_frame_t *phys_to_pframe(void *phys)
{
  return (page_frames_array + phys_to_pframe_id(phys));
}

static inline void clear_page_frame(page_frame_t *page)
{
  page->flags &= ~PF_CLEAR_MASK;
  ASSERT(!(page->flags & PF_LOCK));
  atomic_set(&page->refcount, 0);
}

#define pframe_memnull(page) pframes_memnull(page, 1)

/**
 * @fn static inline void pframe_memnull(page_frame_t *start, int block_size)
 * @brief Fill block of @a block_size continuos pages with zero's
 * @param[out] start - A pointer to very first page frame in the block
 * @param block_size - Number of pages in block
 */
#ifndef ARCH_PAGE_MEMNULL
#include <mstring/string.h>
static inline void pframes_memnull(page_frame_t *start, int block_size)
{
  memset(pframe_to_virt(start), 0, PAGE_SIZE * block_size);
}
#else
#define pframes_memnull(start, block_size) arch_pages_memnull(start, block_size)
#endif /* ARCH_PAGE_MEMNULL */

#ifndef ARCH_COPY_PAGE
static inline void copy_page_frame(page_frame_t *dst, page_frame_t *src)
{
  memcpy(pframe_to_virt(dst), pframe_to_virt(src), PAGE_SIZE);
}
#else
#define copy_page_frame(dst, src) arch_copy_page_frame(dst, src)
#endif /* ARCH_COPY_PAGE */
#endif /* __PAGE_H__ */
