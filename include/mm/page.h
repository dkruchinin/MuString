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

#ifndef __PAGE_H__
#define __PAGE_H__

#include <ds/iterator.h>
#include <ds/list.h>
#include <mlibc/stddef.h>
#include <eza/spinlock.h> /* TODO DK: atomic operations should be placed to the separate file atomic.h */
#include <eza/arch/page.h>
#include <eza/arch/types.h>

#define NOF_MM_POOLS 4

/* memory-related types. */
typedef int page_idx_t;
typedef uint16_t page_flags_t;

#define PF_PDMA       0x01
#define PF_PGP        0x02
#define PF_PCONT      0x04
#define PF_PPERCPU    0x08
#define PF_FREE       0x10
#define PF_RESERVED   0x20
#define PF_KERNEL     0x40
#define PF_DONTCACHE  0x80

#define __pool_type(flags) ((flags) >> 1)
#define PAGE_POOLS_MASK (PF_PGP | PF_PDMA | PF_PPERCPU | PF_PCONT)
#define PAGE_PRESENT_MASK (PAGE_POOL_MASK | PF_RESERVED)

typedef struct __page_frame {
  list_head_t head;
  list_node_t node;
  page_idx_t idx;
  atomic_t refcount;
  uint32_t _private;
  page_flags_t flags;
} page_frame_t;

#define PF_ITER_UNDEF_VAL (-0xf)

DEFINE_ITERATOR(page_frame,
                page_idx_t pf_idx);

DEFINE_ITERATOR_TYPES(page_frame,
                      PF_ITER_ARCH,
                      PF_ITER_INDEX,
                      PF_ITER_LIST,
                      PF_ITER_ALLOC);

extern page_frame_t *page_frames_array;

#define PAGE_ALIGN(addr) align_up((uintptr_t)(addr), PAGE_SIZE)
#define pframe_pool_type(page) (__pool_type((page)->flags & PAGE_POOLS_MASK))

static inline void *pframe_to_virt(page_frame_t *frame)
{
  return (void *)(KERNEL_BASE + frame->idx * PAGE_SIZE);
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
  return (void *)((uintptr_t)frame->idx * PAGE_SIZE);
}

static inline void *virt_to_phys(void *virt)
{
  return virt - KERNEL_BASE;
}

static inline void *pframe_id_to_virt( page_idx_t idx )
{
  return (void *)(KERNEL_BASE + idx * PAGE_SIZE);
}

static inline page_idx_t virt_to_pframe_id(void *virt)
{
  page_idx_t idx = ((uintptr_t)virt - KERNEL_BASE) >> PAGE_WIDTH;
  return idx;
}

#endif /* __PAGE_H__ */
