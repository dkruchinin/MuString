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

#include <config.h>
#include <ds/iterator.h>
#include <ds/list.h>
#include <ds/ttree.h>
#include <mm/page.h>
#include <mm/pfalloc.h>
#include <mm/memobj.h>
#include <mm/mem.h>
#include <mlibc/types.h>
#include <eza/rwsem.h>

#ifdef CONFIG_DEBUG_MM
#define VMM_DBG_NAME_LEN 128
#endif /* CONFIG_DEBUG_MM */

#define VMR_PROT_MASK      (VMR_NONE | VMR_READ | VMR_WRITE | VMR_EXEC | VMR_CANRECPAGES)
#define VMR_FLAGS_OFFS 5

struct __task_struct;

typedef enum __vmrange_flags {
  VMR_NONE         = 0x0001,
  VMR_READ         = 0x0002,
  VMR_WRITE        = 0x0004,
  VMR_EXEC         = 0x0008,  
  VMR_NOCACHE      = 0x0010,
  VMR_FIXED        = 0x0020,
  VMR_ANON         = 0x0040,
  VMR_PRIVATE      = 0x0080,
  VMR_SHARED       = 0x0100,
  VMR_PHYS         = 0x0200,
  VMR_STACK        = 0x0400,
  VMR_POPULATE     = 0x0800,
  VMR_CANRECPAGES  = 0x1000,
  VMR_GATE         = 0x2000,
} vmrange_flags_t;

typedef vmrange_flags_t kmap_flags_t;

#define KMAP_KERN     VMR_NONE
#define KMAP_READ     VMR_READ
#define KMAP_WRITE    VMR_WRITE
#define KMAP_EXEC     VMR_EXEC
#define KMAP_NOCACHE  VMR_NOCACHE
#define KMAP_FLAGS_MASK (KMAP_KERN | KMAP_READ | KMAP_WRITE | KMAP_EXEC | KMAP_NOCACHE)

enum {
  VMM_CLONE_COW      = 0x01,
  VMM_CLONE_POPULATE = 0x02,
  VMM_CLONE_PHYS     = 0x04,
  VMM_CLONE_SHARED   = 0x08,
};

#define VMM_CLONE_MASK (VMM_CLONE_COW | VMM_CLONE_POPULATE | VMM_CLONE_PHYS | VMM_CLONE_SHARED)

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

typedef struct __vmrange {
  struct range_bounds bounds;
  struct __vmm *parent_vmm;
  memobj_t *memobj;
  uintptr_t hole_size;
  list_node_t rmap_node;
  pgoff_t offset;
  vmrange_flags_t flags;  
} vmrange_t;

typedef struct __vmm {
  ttree_t vmranges_tree;
  struct __task_struct *owner;
  rpd_t rpd;
  rwsem_t rwsem;
  ulong_t num_vmrs;
  page_idx_t num_pages;
  page_idx_t max_pages;
#ifdef CONFIG_DEBUG_MM
  char name_dbg[VMM_DBG_NAME_LEN];
#endif /* CONFIG_DEBUG_MM */
} vmm_t;

typedef struct __vmrange_set {
  vmrange_t *vmr;
  uintptr_t va_to;
  uintptr_t va_from;
  ttree_cursor_t cursor;
} vmrange_set_t;


/**
 * @brief Initialize mm internals
 * @note It's an initcall, so it should be called only once during system boot stage.
 */
void mm_initialize(void);
void vmm_initialize(void);
void vmm_subsystem_initialize(void);
int vmm_clone(vmm_t *dst, vmm_t *src, int flags);
void vm_mandmap_register(vm_mandmap_t *mandmap, const char *mandmap_name);
int vm_mandmaps_roll(vmm_t *target_mm);
vmm_t *vmm_create(struct __task_struct *owner);
void vmm_destroy(vmm_t *vmm);
void __clear_vmranges_tree(vmm_t *vmm);
int vmm_handle_page_fault(vmm_t *vmm, uintptr_t addr, uint32_t pfmask);
long vmrange_map(memobj_t *memobj, vmm_t *vmm, uintptr_t addr, page_idx_t npages,
                 vmrange_flags_t flags, pgoff_t offset);
int unmap_vmranges(vmm_t *vmm, uintptr_t va_from, page_idx_t npages);
vmrange_t *vmrange_find(vmm_t *vmm, uintptr_t va_start, uintptr_t va_end, ttree_cursor_t *cursor);
void vmranges_find_covered(vmm_t *vmm, uintptr_t va_from, uintptr_t va_to, vmrange_set_t *vmrs);
int fault_in_user_pages(vmm_t *vmm, uintptr_t address, size_t length, uint32_t pfmask,
                        void (*callback)(vmrange_t *vmr, page_frame_t *page, void *data), void *data);

static inline void vmrange_set_next(vmrange_set_t *vmrs)
{
  if (unlikely(vmrs->va_from >= vmrs->va_to))
    vmrs->vmr = NULL;
  else if (!ttree_cursor_next(&vmrs->cursor)) {
    vmrs->vmr = ttree_item_from_cursor(&vmrs->cursor);
    vmrs->va_from = vmrs->vmr->bounds.space_end;
  }
  else
    vmrs->vmr = NULL;
}

static inline pgoff_t addr2pgoff(vmrange_t *vmr, uintptr_t addr)
{
  return (vmr->offset +
          ((PAGE_ALIGN_DOWN(addr) - vmr->bounds.space_start) >> PAGE_WIDTH));
}

static inline uintptr_t pgoff2addr(vmrange_t *vmr, pgoff_t offset)
{
  return (vmr->bounds.space_start +
          ((uintptr_t)(offset - vmr->offset) << PAGE_WIDTH));
}

struct mmap_args;

/**
 * @fn status_t sys_mmap(uintptr_t addr,size_t size,uint32_t flags,shm_id_t fd,uintptr_t offset);
 * @brief mmap (shared) memory *
 * @param addr - address where you want to map memory
 * @param size - size of memory to map
 * @param flags - mapping flags
 * @param fd - id of shared memory area
 * @param offset - offset of the shared area to map
 *
 */
long sys_mmap(pid_t victim, memobj_id_t memobj_id, struct mmap_args *uargs);
int sys_munmap(pid_t victim, uintptr_t addr, size_t length);
int sys_grant_pages(uintptr_t va_from, size_t length, pid_t target_pid, uintptr_t target_addr);

#ifdef CONFIG_DEBUG_MM
static inline char *vmm_get_name_dbg(vmm_t *vmm)
{
  return vmm->name_dbg;
}

static inline void vmm_set_name_dbg(vmm_t *vmm, const char *name)
{
  strncpy(vmm->name_dbg, name, VMM_DBG_NAME_LEN);
}

void vmm_set_name_from_pid_dbg(vmm_t *vmm);
void vmm_enable_verbose_dbg(void);
void vmm_disable_verbose_dbg(void);
void vmranges_print_tree_dbg(vmm_t *vmm);
#else
#define vmm_get_name_dbg(vmm) "NONAME"
#define vmm_set_name_dbg(vmm, name)
#define vmm_set_name_from_pid_dbg(vmm)
#define vmranges_print_tree_dbg(vmm)
#endif /* CONFIG_DEBUG_MM */

#endif /* __VMM_H__ */
