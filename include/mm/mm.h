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
 * mm/mm.c: Contains types and prototypes for the kernel memory manager.
 *
 */


#ifndef __MM_H__
#define __MM_H__

#include <ds/list.h>
#include <eza/arch/types.h>
#include <eza/spinlock.h>
#include <eza/arch/e820map.h>
#include <eza/arch/page.h>
//#include <eza/swks.h>

extern int _kernel_end;
extern int _kernel_start;

#define KERNEL_FIRST_FREE_ADDRESS ((char *)&_kernel_end)
#define KERNEL_FIRST_ADDRESS ((char *)&_kernel_start)

#define NUM_MEMORY_ZONES  2

typedef enum __page_frame_flags {
  PF_KERNEL_PAGE = 0x1,
  PF_IO_PAGE = 0x2,
} page_frame_flags_t;

typedef enum __memory_zone_type {
  ZONE_DMA = 0,
  ZONE_NORMAL = 1,
} memory_zone_type_t;

#define PAGE_ZONE_MASK 0x3

#define PAGE_RESERVED  (1 << 2)  /* Page is reserved for some kernel purposes. */
#define PERMANENT_PAGE_FLAG_MASK  (PAGE_ZONE_MASK | PAGE_RESERVED)

typedef enum __page_alloc_flags {
  PAF_DMA_PAGE = 1,
} page_alloc_flags_t;

#define GENERIC_KERNEL_PAGE  0 /* Flags for default kernel page allocation */

typedef struct __memory_zone {
  page_idx_t num_total_pages, num_free_pages, num_reserved_pages;
  list_head_t pages, reserved_pages;
  spinlock_t lock;
  memory_zone_type_t type;
} memory_zone_t;

typedef struct __page_frame {
  list_node_t page_next;
  list_head_t active_list;
  page_idx_t idx;
  atomic_t refcount;
  page_flags_t flags;
} page_frame_t;

typedef struct __percpu_page_cache {
  spinlock_t lock;
  list_head_t pages;
  page_idx_t num_free_pages, total_pages;
} percpu_page_cache_t;

extern page_frame_t *page_frames_array;
extern page_idx_t max_direct_page;

/* Main page array. */
extern page_frame_t *page_frames_array;

/* Main memory zones statistics. */
extern memory_zone_t memory_zones[NUM_MEMORY_ZONES];

void build_page_array(void);
void remap_memory(void);

void arch_clean_page(page_frame_t *frame);

#endif

