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
 * include/mm/vmm.h - Architecture independend memory mapping API
 *
 */

#ifndef __VMM_H__
#define __VMM_H__

#include <ds/iterator.h>
#include <ds/list.h>
#include <ds/ttree.h>
#include <mm/page.h>
#include <mm/pfalloc.h>
#include <mm/memobj.h>
#include <mlibc/types.h>
#include <eza/arch/mm.h>
#include <eza/arch/ptable.h>

typedef struct __mmap_info {
  uintptr_t va_from;
  uintptr_t va_to;
  page_frame_iterator_t *pfi;
  uint32_t ptable_flags;
} mmap_info_t;

#define VMR_PROTO_MASK (VMR_NONE | VMR_READ | VMR_WRITE | VMR_EXEC)

typedef enum __vmrange_flags {
  VMR_NONE     = 0x0001,
  VMR_READ     = 0x0002,
  VMR_WRITE    = 0x0004,
  VMR_EXEC     = 0x0008,
  VMR_NOCACHE  = 0x0010,
  VMR_FIXED    = 0x0020,
  VMR_ANON     = 0x0040,
  VMR_PRIVATE  = 0x0080,
  VMR_SHARED   = 0x0100,
  VMR_PHYS     = 0x0200,
  VMR_STACK    = 0x0400,
  VMR_POPULATE = 0x0800,
  VMR_GENERIC  = 0x1000,
} vmrange_flags_t;

typedef vmrange_flags_t kmap_flags_t;

#define KMAP_KERN    VMR_NONE
#define KMAP_READ    VMR_READ
#define KMAP_WRITE   VMR_WRITE
#define KMAP_EXEC    VMR_EXEC
#define KMAP_NOCACHE VMR_NOCACHE
#define KMAP_FLAGS_MASK (KMAP_KERN | KMAP_READ | KMAP_WRITE | KMAP_EXEC | KMAP_NOCACHE)

struct __vmm;
struct range_bounds {
  uintptr_t space_start;
  uintptr_t space_end;
};

typedef struct __vm_mandmap {
  char *name;
  list_node_t node;
  uintptr_t virt_addr;
  uintptr_t phys_addr;
  ulong_t num_pages;
  kmap_flags_t flags;  
} vm_mandmap_t;

typedef struct __vmragnge {
  struct range_bounds bounds;
  struct __vmm *parent_vmm;
  memobj_t *memobj;
  void *private;
  vmrange_flags_t flags;
} vmrange_t;

typedef struct __vmm {
  ttree_t vmranges_tree;
  vmrange_t *cached_vmr;
  vmrange_t *lru_range;
  atomic_t vmm_users;
  rpd_t rpd;
  uintptr_t aspace_start;
  ulong_t num_vmrs;
  page_idx_t num_pages;
  page_idx_t max_pages;
} vmm_t;

extern rpd_t kernel_rpd;

#define valid_user_address(va)                  \
  valid_user_address_range(va, 0)

static inline bool valid_user_address_range(uintptr_t va_start, uintptr_t length)
{
  return ((va_start >= USPACE_VA_BOTTOM) &&
          ((va_start + length) < USPACE_VA_TOP));
}

static inline void unpin_page_frame(page_frame_t *pf)
{
  ASSERT(atomic_get(&pf->refcount) > 0);
  atomic_dec(&pf->refcount);
  if (!atomic_get(&pf->refcount))
    free_pages(pf);
}

static inline void pin_page_frame(page_frame_t *pf)
{
  atomic_inc(&pf->refcount);
}

#define mmap_kern(va, first_page, npages, flags)    \
  mmap_core(&kernel_rpd, va, first_page, npages, flags)

/**
 * @brief Initialize mm internals
 * @note It's an initcall, so it should be called only once during system boot stage.
 */
void vmm_initialize(void);
void vm_mandmap_register(vm_mandmap_t *mandmap, const char *mandmap_name);
int vm_mandmaps_roll(vmm_t *target_mm);

vmm_t *vmm_create(void);
int mmap_core(rpd_t *rpd, uintptr_t va, page_idx_t first_page, page_idx_t npages, kmap_flags_t flags);

static inline void munmap_core(rpd_t *rpd, uintptr_t va, page_idx_t npages)
{
  ptable_unmap(rpd, va, npages);
}

#endif /* __VMM_H__ */
