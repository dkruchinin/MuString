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
 */

#ifndef __MSTRING_MEM_H__
#define __MSTRING_MEM_H__

#include <config.h>
#include <arch/mem.h>
#include <arch/ptable.h>
#include <mm/page.h>
#include <mm/page_alloc.h>
#include <mm/mmpool.h>
#include <sync/spinlock.h>
#include <mstring/types.h>

struct __vmm;
typedef struct __rpd {
  void *root_dir;
  spinlock_t rpd_lock;
  struct __vmm *vmm;
} rpd_t;

struct pt_ops {
  int (*map_page)(rpd_t *rpd, uintptr_t vaddr,
                  page_idx_t pidx, ptable_flags_t flags);
  void (*unmap_page)(rpd_t *rpd, uintptr_t vaddr);
  page_idx_t (*vaddr_to_pidx)(rpd_t *rpd, uintptr_t vaddr,
                              /* OUT */ pde_t **retpde);
  void *(*alloc_pagedir)(void);
  void (*free_pagedir)(void *pdir);
  int (*root_pdir_init_arch)(rpd_t *rpd);
  void (*root_pdir_deinit_arch)(rpd_t *rpd);
};

extern struct pt_ops pt_ops;

#define pagetable_lock(rpd) spinlock_lock(&(rpd)->rpd_lock)
#define pagetable_unlock(rpd) spinlock_unlock(&(rpd)->rpd_lock)

static inline int initialize_rpd(rpd_t *rpd, struct __vmm *vmm)
{
  spinlock_initialize(&rpd->rpd_lock);
  rpd->vmm = vmm;
  return pt_ops.root_pdir_init_arch(rpd);
}

static inline void deinitialize_rpd(rpd_t *rpd)
{  
  pt_ops.root_pdir_deinit_arch(rpd);
}

extern rpd_t kernel_root_pdir;
extern struct pt_ops pt_ops;

typedef enum kmap_flags {
  KMAP_KERN    = 0x01,
  KMAP_READ    = 0x02,
  KMAP_WRITE   = 0x04,
  KMAP_EXEC    = 0x08,
  KMAP_NOCACHE = 0x10,
} kmap_flags_t;

#define KMAP_OFFSET 5
#define KERNEL_ROOT_PDIR() (&kernel_root_pdir)

INITCODE void mem_init(void);
rpd_t *root_pdir_allocate(void);
void root_pdir_free(rpd_t *rpd);


static inline int mmap_page(rpd_t *rpd, uintptr_t addr,
                            page_idx_t pidx, kmap_flags_t flags)
{
  return pt_ops.map_page(rpd, addr, pidx,
                         kmap_to_ptable_flags(flags));
}

static inline void munmap_page(rpd_t *rpd, uintptr_t addr)
{
  pt_ops.unmap_page(rpd, addr);
}

static inline page_idx_t __vaddr_to_pidx(rpd_t *rpd, uintptr_t addr,
                                         /* OUT */ pde_t **pde)
{
  return pt_ops.vaddr_to_pidx(rpd, addr, pde);
}

static inline int vaddr_to_pidx(rpd_t *rpd, uintptr_t addr)
{
  return __vaddr_to_pidx(rpd, addr, NULL);
}

static inline bool page_is_mapped(rpd_t *rpd, uintptr_t va)
{
  return (vaddr_to_pidx(rpd, va) != PAGE_IDX_INVAL);
}

static inline void unpin_page_frame(page_frame_t *pf)
{
  if((uintptr_t)pframe_to_virt(pf) == 0xffffffff836dc000UL) {
    kprintf("REFCOUNT = %d\n", atomic_get(&pf->refcount));
  }
  if (atomic_dec_and_test(&pf->refcount)) {
    free_page(pf);
  }
}

static inline void pin_page_frame(page_frame_t *pf)
{
  atomic_inc(&pf->refcount);
}

#define valid_user_address(va)                  \
  valid_user_address_range(va, 0)

static inline bool valid_user_address_range(uintptr_t va_start,
                                            uintptr_t length)
{
    return ((va_start >= USPACE_VADDR_BOTTOM) &&
            ((va_start + length) <= USPACE_VADDR_TOP));
}

static inline void *user_to_kernel_vaddr(rpd_t *rpd, uintptr_t addr)
{  
  page_idx_t idx;

  idx = vaddr_to_pidx(rpd, addr);
  if (idx == PAGE_IDX_INVAL)
    return NULL;

  return ((char *)pframe_to_virt(pframe_by_id(idx)) +
          (addr - PAGE_ALIGN_DOWN(addr)));
}

#define lock_page_frame(p, bit)                 \
  spinlock_lock_bit(&(p)->flags, BITNUM(bit))
#define unlock_page_frame(p, bit)               \
  spinlock_unlock_bit(&(p)->flags, BITNUM(bit))

struct __vmrange;

int mmap_kern(uintptr_t va_from, page_idx_t first_page, pgoff_t npages, long flags);
int handle_copy_on_write(struct __vmrange *vmr, uintptr_t addr,
                         page_frame_t *dst_page, page_frame_t *src_page);
#endif /* __MSTRING_MEM_H__ */
