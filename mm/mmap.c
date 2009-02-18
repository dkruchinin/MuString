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
 * mm/mmap.c - Architecture independend memory mapping API
 *
 */

#include <config.h>
#include <ds/list.h>
#include <ds/ttree.h>
#include <ds/iterator.h>
#include <mm/page.h>
#include <mm/pfalloc.h>
#include <mm/slab.h>
#include <mm/memobj.h>
#include <mm/vmm.h>
#include <mm/pfi.h>
#include <eza/mutex.h>
#include <eza/rwsem.h>
#include <eza/spinlock.h>
#include <eza/security.h>
#include <kernel/syscalls.h>
#include <mlibc/kprintf.h>
#include <mlibc/types.h>

rpd_t kernel_rpd;
static memcache_t *__vmms_cache = NULL;
static memcache_t *__vmrs_cache = NULL;
static LIST_DEFINE(__mandmaps_lst);
static int __num_mandmaps = 0;

#ifdef CONFIG_DEBUG_MM
static bool __vmm_verbose = false;
static SPINLOCK_DEFINE(__vmm_verb_lock);

#define VMM_VERBOSE(fmt, args...)               \
  do {                                          \
    if (__vmm_verbose) {                        \
      kprintf("[VMM VERBOSE]: ");               \
      kprintf(fmt, ##args);                     \
    }                                           \
  } while (0)

#else
#define VMM_VERBOSE(fmt, args...)
#endif /* CONFING_DEBUG_MM */

static int __vmranges_cmp(void *r1, void *r2)
{
  struct range_bounds *range1, *range2;

  range1 = (struct range_bounds *)r1;
  range2 = (struct range_bounds *)r2;
  if ((long)(range2->space_start - range1->space_end) < 0) {
    if ((long)(range1->space_start - range2->space_end) >= 0)
      return 1;
     
    return 0;
  }
  else
    return -1;
}

#define __VMR_CLEAR_MASK (VMR_FIXED | VMR_POPULATE)

static inline bool can_be_merged(const vmrange_t *vmr, const memobj_t *memobj, vmrange_flags_t flags)
{
    return (((vmr->flags & ~__VMR_CLEAR_MASK) == (flags & ~__VMR_CLEAR_MASK))
          && ((vmr->memobj == memobj)));
}

static vmrange_t *create_vmrange(vmm_t *parent_vmm, uintptr_t va_start,
                                 page_idx_t npages, vmrange_flags_t flags)
{
  vmrange_t *vmr;

  vmr = alloc_from_memcache(__vmrs_cache);
  if (!vmr)
    return NULL;

  vmr->parent_vmm = parent_vmm;
  vmr->flags = flags;
  vmr->bounds.space_start = va_start;
  vmr->bounds.space_end = va_start + (npages << PAGE_WIDTH);
  vmr->memobj = NULL;
  vmr->hole_size = vmr->offset = 0;
  parent_vmm->num_vmrs++;

  return vmr;
}

static void destroy_vmrange(vmrange_t *vmr)
{
  vmr->parent_vmm->num_vmrs--;
  memfree(vmr);
}

static vmrange_t *merge_vmranges(vmrange_t *prev_vmr, vmrange_t *next_vmr)
{
  ASSERT(prev_vmr->parent_vmm == next_vmr->parent_vmm);
  ASSERT(can_be_merged(prev_vmr, next_vmr->memobj, next_vmr->flags));
  ASSERT(prev_vmr->bounds.space_end == next_vmr->bounds.space_start);

  VMM_VERBOSE("%s: Merge two VM ranges [%p, %p) and [%p, %p).\n",
              vmm_get_name_dbg(prev_vmr->parent_vmm),
              prev_vmr->bounds.space_start, prev_vmr->bounds.space_end,
              next_vmr->bounds.space_start, next_vmr->bounds.space_end);
  ttree_delete(&prev_vmr->parent_vmm->vmranges_tree, &next_vmr->bounds);
  prev_vmr->bounds.space_end = next_vmr->bounds.space_end;
  prev_vmr->hole_size = next_vmr->hole_size;
  destroy_vmrange(next_vmr);

  return prev_vmr;
}

static vmrange_t *split_vmrange(vmrange_t *vmrange, uintptr_t va_from, uintptr_t va_to)
{
  vmrange_t *new_vmr;
  page_idx_t npages;

  ASSERT(vmrange->parent_vmm != NULL);
  ASSERT(!(va_from & PAGE_MASK));
  ASSERT(!(va_to & PAGE_MASK));
  npages = (vmrange->bounds.space_end - va_to) >> PAGE_WIDTH;
  new_vmr = create_vmrange(vmrange->parent_vmm, va_to, npages, vmrange->flags);
  if (!new_vmr)
    return NULL;

  VMM_VERBOSE("%s: Split VM range [%p, %p) to [%p, %p) and [%p, %p)\n",
              vmm_get_name_dbg(vmrange->parent_vmm), vmrange->bounds.space_start,
              vmrange->bounds.space_end, vmrange->bounds.space_start,
              va_from, va_to, vmrange->bounds.space_end);
  vmrange->bounds.space_end = va_from;
  new_vmr->memobj = vmrange->memobj;
  new_vmr->hole_size = vmrange->hole_size;
  vmrange->hole_size = new_vmr->bounds.space_start - vmrange->bounds.space_end;
  ttree_insert(&vmrange->parent_vmm->vmranges_tree, new_vmr);

  return new_vmr;
}

static void fix_vmrange_holes(vmm_t *vmm, vmrange_t *vmrange, ttree_cursor_t *cursor)
{
  vmrange_t *vmr;
  ttree_cursor_t csr;
  
  ttree_cursor_copy(&csr, cursor);
  if (!ttree_cursor_prev(&csr)) {
    vmr = ttree_item_from_cursor(&csr);
    VMM_VERBOSE("%s(P) [%p, %p): old hole size: %ld, new hole size: %ld\n",
                vmm_get_name_dbg(vmm), vmr->bounds.space_start, vmr->bounds.space_end,
                vmr->hole_size, vmrange->bounds.space_start - vmr->bounds.space_end);
    vmr->hole_size = vmrange->bounds.space_start - vmr->bounds.space_end;
  }

  ttree_cursor_copy(&csr, cursor);
  if (!ttree_cursor_next(&csr)) {
    VMM_VERBOSE("%s(N) [%p, %p): old hole size: %ld, new hole size: %ld\n",
                vmm_get_name_dbg(vmm), vmrange->bounds.space_start, vmrange->bounds.space_end,
                vmrange->hole_size, vmr->bounds.space_start - vmrange->bounds.space_end);
    vmr = ttree_item_from_cursor(&csr);
    vmrange->hole_size = vmr->bounds.space_start - vmrange->bounds.space_end;
  }
  else {
    VMM_VERBOSE("%s(L) [%p, %p): old hole size: %ld, new hole size: %ld\n",
                vmm_get_name_dbg(vmm), vmrange->bounds.space_start, vmrange->bounds.space_end,
                vmrange->hole_size, USPACE_VA_TOP - vmrange->bounds.space_end);
    vmrange->hole_size = USPACE_VA_TOP - vmrange->bounds.space_end;
  }
}

static int __map_phys_pages(vmm_t *vmm, uintptr_t va, uintptr_t phys,
                            page_idx_t npages, kmap_flags_t flags)
{
  page_frame_iterator_t pfi;
  ITERATOR_CTX(page_frame, PF_ITER_INDEX) index_ctx;

  pfi_index_init(&pfi, &index_ctx, phys >> PAGE_WIDTH,
                 (phys >> PAGE_WIDTH) + npages - 1);  
  iter_first(&pfi);
  return __mmap_core(&vmm->rpd, va, npages, &pfi, flags);
}

void vm_mandmap_register(vm_mandmap_t *mandmap, const char *mandmap_name)
{
#ifndef CONFIG_TEST
  ASSERT(!valid_user_address_range(mandmap->virt_addr, mandmap->num_pages << PAGE_WIDTH));
#endif /* CONFIG_TEST */
  
  mandmap->name = (char *)mandmap_name;
  mandmap->flags &= KMAP_FLAGS_MASK;
  list_add2tail(&__mandmaps_lst, &mandmap->node);
  __num_mandmaps++;
}

int vm_mandmaps_roll(vmm_t *target_mm)
{
  int status = 0;
  vm_mandmap_t *mandmap;

  list_for_each_entry(&__mandmaps_lst, mandmap, node) {
    status = mmap_core(&target_mm->rpd, mandmap->virt_addr, mandmap->phys_addr >> PAGE_WIDTH,
                       mandmap->num_pages, mandmap->flags);
    VMM_VERBOSE("[%s]: Mandatory mapping \"%s\" [%p -> %p] was initialized\n",
                vmm_get_name_dbg(target_mm), mandmap->name,
                mandmap->virt_addr, mandmap->virt_addr + (mandmap->num_pages << PAGE_WIDTH));
    if (status) {
      kprintf(KO_ERROR "Mandatory mapping \"%s\" failed [err=%d]: Can't mmap region %p -> %p starting"
              " from page with index #%x!\n",
              mandmap->name, status, mandmap->virt_addr, mandmap->virt_addr + (mandmap->num_pages << PAGE_WIDTH),
              mandmap->phys_addr << PAGE_WIDTH);
      goto out;
    }
  }

  out:
  return status;
}

void vmm_subsystem_initialize(void)
{
  kprintf("[MM] Initializing VMM subsystem...\n");
  __vmms_cache = create_memcache("VMM objects cache", sizeof(vmm_t),
                                 DEFAULT_SLAB_PAGES, SMCF_PGEN | SMCF_GENERIC);
  if (!__vmms_cache)
    panic("vmm_subsystem_initialize: Can not create memory cache for VMM objects. ENOMEM");

  __vmrs_cache = create_memcache("Vmrange objects cache", sizeof(vmrange_t),
                                 DEFAULT_SLAB_PAGES, SMCF_PGEN | SMCF_GENERIC);
  if (!__vmrs_cache)
    panic("vmm_subsystem_initialize: Can not create memory cache for vmrange objects. ENOMEM.");
}

vmm_t *vmm_create(void)
{
  vmm_t *vmm;

  vmm = alloc_from_memcache(__vmms_cache);
  if (!vmm)
    return NULL;

  memset(vmm, 0, sizeof(*vmm));
  ttree_init(&vmm->vmranges_tree, __vmranges_cmp, vmrange_t, bounds);
  atomic_set(&vmm->vmm_users, 1);
  rwsem_initialize(&vmm->rwsem);
  if (ptable_ops.initialize_rpd(&vmm->rpd) < 0) {
    memfree(vmm);
    vmm = NULL;
  }

  return vmm;
}

uintptr_t find_free_vmrange(vmm_t *vmm, uintptr_t length, ttree_cursor_t *cursor)
{    
  uintptr_t start = USPACE_VA_BOTTOM;
  vmrange_t *vmr;
  ttree_node_t *tnode = NULL;
  int i = 0;

  tnode = ttree_tnode_leftmost(vmm->vmranges_tree.root);
  if (unlikely(tnode == NULL))
    goto found;

  /*
   * At first check if there is enough space between
   * the start of the very first vmrange and the bottom
   * userspace virtual address. If so, we're really lucky guys ;)   
   */
  vmr = ttree_key2item(&vmm->vmranges_tree, tnode_key_min(tnode));
  if ((start - vmr->bounds.space_start) >= length) {
    i = tnode->min_idx;
    goto found;
  }
  while (tnode) {    
    tnode_for_each_index(tnode, i) {
      vmr = ttree_key2item(&vmm->vmranges_tree, tnode_key(tnode, i));
      if (vmr->hole_size >= length) {
        start = vmr->bounds.space_end;
        goto found;
      }
    }

    tnode = tnode->successor;
  }

  VMM_VERBOSE("%s: Failed to find free VM range with size = %d pages (%d)\n",
              vmm_get_name_dbg(vmm), length << PAGE_WIDTH, length);
  return INVALID_ADDRESS;
  
  found:
  if (cursor) {
    ttree_cursor_init(&vmm->vmranges_tree, cursor);
    if (tnode) {
      cursor->tnode = tnode;
      cursor->idx = i;
      cursor->side = TNODE_BOUND;
      cursor->state = TT_CSR_TIED;
      ttree_cursor_next(cursor);
      cursor->state = TT_CSR_PENDING;
    }
  }

  return start;
}

vmrange_t *vmrange_find(vmm_t *vmm, uintptr_t va_start, uintptr_t va_end, ttree_cursor_t *cursor)
{
  struct range_bounds bounds;
  vmrange_t *vmr;

  bounds.space_start = va_start;
  bounds.space_end = va_end;
  vmr = ttree_lookup(&vmm->vmranges_tree, &bounds, cursor);
  return vmr;
}

void vmranges_find_covered(vmm_t *vmm, uintptr_t va_from, uintptr_t va_to, vmrange_set_t *vmrs)
{
  ASSERT(!(va_from & PAGE_MASK) && !(va_to & PAGE_MASK));
  memset(vmrs, 0, sizeof(*vmrs));
  vmrs->va_from = va_from;
  vmrs->va_to = va_to;
  ttree_cursor_init(&vmm->vmranges_tree, &vmrs->cursor);
  vmrs->vmr = vmrange_find(vmm, va_from, va_from + PAGE_SIZE, &vmrs->cursor);
  if (!vmrs->vmr) {
    if (!ttree_cursor_next(&vmrs->cursor)) {
      vmrs->vmr = ttree_item_from_cursor(&vmrs->cursor);
      if (vmrs->vmr->bounds.space_end > va_to)
        vmrs->vmr = NULL;
    }
  }
}

long vmrange_map(memobj_t *memobj, vmm_t *vmm, uintptr_t addr, page_idx_t npages,
                 vmrange_flags_t flags, page_idx_t offs_pages)
{
  vmrange_t *vmr;
  ttree_cursor_t cursor, csr_tmp;
  int err = 0;
  bool was_merged = false;

  vmr = NULL;
  ASSERT(memobj != NULL);
  ttree_cursor_init(&vmm->vmranges_tree, &cursor);
  if (!(flags & VMR_PROTO_MASK)
      || !(flags & (VMR_PRIVATE | VMR_SHARED))
      || ((flags & (VMR_PRIVATE | VMR_SHARED)) == (VMR_PRIVATE | VMR_SHARED))
      || !npages
      || ((flags & VMR_NONE) && ((flags & VMR_PROTO_MASK) != VMR_NONE))) {
    err = -EINVAL;
    goto err;
  }
  if (flags & VMR_PHYS) {
    flags |= VMR_NOCACHE;
    if (!trusted_task(current_task())) {
      err = -EPERM;
      goto err;
    }
    if (memobj != &null_memobj) {
      err = -EINVAL;
      goto err;
    }
  }
  if (addr) {
    if (!(flags & VMR_PHYS) &&
        !valid_user_address_range(addr, (npages << PAGE_WIDTH))) {
      if (flags & VMR_FIXED) {
        err = -ENOMEM;
        goto err;
      }
    }

    vmr = vmrange_find(vmm, addr, addr + (npages << PAGE_WIDTH), &cursor);
    if (vmr) {
      if (flags & VMR_FIXED) {
        VMM_VERBOSE("%s: Failed to allocate fixed VM range [%p, %p) because it is "
                    "covered by this one: [%p, %p)\n", vmm_get_name_dbg(vmm), addr,
                    addr + (npages << PAGE_WIDTH), vmr->bounds.space_start, vmr->bounds.space_end);
        err = -EINVAL;
        goto err;
      }
    }
    else
      goto create_vmrange;
  }

  /*
   * It has been asked to find suitable portion of address space
   * satisfying requested length.
   */
  addr = find_free_vmrange(vmm, (npages << PAGE_WIDTH), &cursor);
  if (addr == INVALID_ADDRESS) {
    err = -ENOMEM;
    goto err;
  }

  create_vmrange:
  ttree_cursor_copy(&csr_tmp, &cursor);
  vmr = NULL;
  if (!ttree_cursor_prev(&csr_tmp)) {
    vmr = ttree_item_from_cursor(&csr_tmp);
    if (can_be_merged(vmr, memobj, flags) && (vmr->bounds.space_end == addr)) {
      VMM_VERBOSE("%s: Attach [%p, %p) to the top of [%p, %p)\n",
                  vmm_get_name_dbg(vmm), addr, addr + (npages << PAGE_WIDTH),
                  vmr->bounds.space_start, vmr->bounds.space_end);
      vmr->hole_size -= (addr + (npages << PAGE_WIDTH) - vmr->bounds.space_end);
      vmr->bounds.space_end = addr + (npages << PAGE_WIDTH);
      was_merged = true;
    }
  }

  ttree_cursor_copy(&csr_tmp, &cursor);
  if (!ttree_cursor_next(&csr_tmp)) {
    vmrange_t *prev = vmr;

    vmr = ttree_item_from_cursor(&csr_tmp);
    if (can_be_merged(vmr, memobj, flags)) {
      if (!was_merged && (vmr->bounds.space_start == (addr + (npages << PAGE_WIDTH)))) {
        VMM_VERBOSE("%s: Attach [%p, %p) to the bottom of [%p, %p)\n",
                    vmm_get_name_dbg(vmm), addr, addr + (npages << PAGE_WIDTH),
                    vmr->bounds.space_start, vmr->bounds.space_end);
        vmr->bounds.space_start = addr;
        was_merged = true;
        if (prev)
          prev->hole_size = vmr->bounds.space_start - prev->bounds.space_end;
      }
      else if (was_merged && (prev->bounds.space_end == vmr->bounds.space_start))
        merge_vmranges(prev, vmr);
    }
  }
  if (!was_merged) {
    vmr = create_vmrange(vmm, addr, npages, flags);
    if (!vmr) {
      err = -ENOMEM;
      goto err;
    }
    
    ttree_insert_placeful(&cursor, vmr);
  }

  vmr->memobj = memobj;
  if (flags & VMR_PHYS) {
    err = __map_phys_pages(vmm, addr, offs_pages << PAGE_WIDTH, npages, flags & KMAP_FLAGS_MASK);
    if (err)
      goto err;
  }  
  else if (flags & VMR_POPULATE) {
    mutex_lock(&memobj->mutex);
    err = memobj->mops.populate_pages(memobj, vmr, addr, npages, offs_pages);
    mutex_unlock(&memobj->mutex);
    if (err)
      goto err;
  }
  if (!was_merged)
    fix_vmrange_holes(vmm, vmr, &cursor);

  return (!(flags & VMR_STACK) ? addr : (addr + (npages << PAGE_WIDTH)));
  
  err:
  if (vmr) {
    ttree_delete_placeful(&cursor);
    destroy_vmrange(vmr);
  }

  return err;
}

void unmap_vmranges(vmm_t *vmm, uintptr_t va_from, page_idx_t npages)
{
  ttree_cursor_t cursor;
  uintptr_t va_to = va_from + (npages << PAGE_WIDTH);
  vmrange_t *vmr;

  ASSERT(!(va_from & PAGE_MASK));
  ttree_cursor_init(&vmm->vmranges_tree, &cursor);
  vmr = vmrange_find(vmm, va_from, va_from + PAGE_SIZE, &cursor);
  if (!vmr) {
    if (!ttree_cursor_next(&cursor))
      vmr = ttree_item_from_cursor(&cursor);
    else
      return;
  }
  if ((vmr->bounds.space_start < va_from) &&
      (vmr->bounds.space_end > va_to)) {
    if (!split_vmrange(vmr, va_from, va_to)) {
      kprintf(KO_ERROR "unmap_vmranges: Failed to split VM range [%p, %p). -ENOMEM\n",
              vmr->bounds.space_start, vmr->bounds.space_end);
    }

    munmap_core(&vmm->rpd, va_from, npages);
    return;
  }
  else if (va_from > vmr->bounds.space_start) {
    vmr->hole_size += vmr->bounds.space_end - va_from;    
    munmap_core(&vmm->rpd, va_from, vmr->bounds.space_end);
    vmr->bounds.space_end = va_from;
  }

  va_from = vmr->bounds.space_end;
  if (ttree_cursor_next(&cursor) < 0)
    return;
  while (va_from < va_to) {
    vmr = ttree_item_from_cursor(&cursor);
    munmap_core(&vmm->rpd, va_from, ((va_to - va_from) << PAGE_WIDTH));
    if (unlikely(va_to < vmr->bounds.space_end)) {
      ttree_cursor_t csr_prev;

      ttree_cursor_copy(&csr_prev, &cursor);
      vmr->bounds.space_start = va_to;
      if (!ttree_cursor_prev(&csr_prev)) {
        vmrange_t *vmr_prev = ttree_item_from_cursor(&csr_prev);
        vmr_prev->hole_size = vmr->bounds.space_start - vmr_prev->bounds.space_end;
      }

      break;
    }

    ttree_delete_placeful(&cursor);
    va_from = vmr->bounds.space_end;
    destroy_vmrange(vmr);
  }
}

int mmap_core(rpd_t *rpd, uintptr_t va, page_idx_t first_page, page_idx_t npages, kmap_flags_t flags)
{
  page_frame_iterator_t pfi;
  ITERATOR_CTX(page_frame, PF_ITER_INDEX) pf_idx_ctx;

  if ((va & PAGE_MASK) || !npages)
    return -EINVAL;

  pfi_index_init(&pfi, &pf_idx_ctx, first_page, first_page + npages - 1);
  iter_first(&pfi);
  return __mmap_core(rpd, va, npages, &pfi, flags);
}

int vmm_handle_page_fault(vmrange_t *vmr, uintptr_t addr, uint32_t pfmask)
{
  memobj_t *memobj = vmr->memobj;  
  int ret;
  off_t off;

  ASSERT(memobj != NULL);
  off = addr2memobj_offs(vmr, addr);
  if (((pfmask & PFLT_WRITE) &&
       ((vmr->flags & (VMR_READ | VMR_WRITE)) == VMR_READ))
      || (vmr->flags & VMR_NONE)) {
    return -EACCES;
  }

  mutex_lock(&memobj->mutex);
  ret = memobj->mops.handle_page_fault(memobj, vmr, off, pfmask);
  mutex_unlock(&memobj->mutex);
  return ret;
}

long sys_mmap(uintptr_t addr, size_t size, int prot, int flags, int memobj_id, off_t offset)
{
  memobj_t *memobj = NULL;
  vmm_t *vmm = current_task()->task_mm;
  long ret;
  vmrange_flags_t vmrflags = (prot & VMR_PROTO_MASK) | (flags << VMR_FLAGS_OFFS);
  
  if ((vmrflags & VMR_ANON) || (memobj_id == NULL_MEMOBJ_ID)) {
    memobj = memobj_find_by_id(NULL_MEMOBJ_ID);
    ASSERT(memobj != NULL);    
  }
  else if (memobj_id) {
    if ((offset & PAGE_MASK) || (vmrflags & VMR_ANON))
      return -EINVAL;
    
    memobj = memobj_find_by_id(memobj_id);
    if (!memobj)
      return -ENOENT;
  }
  if (!size || ((vmrflags & VMR_FIXED) && (addr & PAGE_MASK)) ||
      ((vmrflags & VMR_PHYS) && (offset & PAGE_MASK))) {
      return -EINVAL;
  }
  
  rwsem_down_write(&vmm->rwsem);
  ret = vmrange_map(memobj, vmm, addr, PAGE_ALIGN(size) >> PAGE_WIDTH,
                    vmrflags, PAGE_ALIGN(offset) >> PAGE_WIDTH);
  rwsem_up_write(&vmm->rwsem);
  return ret;
}

void sys_munmap(uintptr_t addr, size_t length)
{
  vmm_t *vmm = current_task()->task_mm;
  
  addr = PAGE_ALIGN_DOWN(addr);
  length = PAGE_ALIGN_DOWN(length);
  if (!addr || !length || !valid_user_address_range(addr, length))
    return;

  rwsem_down_write(&vmm->rwsem);
  unmap_vmranges(vmm, addr, length >> PAGE_WIDTH);
  rwsem_up_write(&vmm->rwsem);
}

#ifdef CONFIG_DEBUG_MM
#include <eza/task.h>

void vmm_set_name_from_pid_dbg(vmm_t *vmm, unsigned long pid)
{
  if (is_tid(pid))
    pid = TID_TO_PIDBASE(pid);
  
  memset(vmm->name_dbg, 0, VMM_DBG_NAME_LEN);  
  snprintf(vmm->name_dbg, VMM_DBG_NAME_LEN, "VMM [PID: %ld]", pid);
}

void vmm_enable_verbose_dbg(void)
{
  spinlock_lock(&__vmm_verb_lock);
  __vmm_verbose = true;
  spinlock_unlock(&__vmm_verb_lock);
}

void vmm_disable_verbose_dbg(void)
{
  spinlock_lock(&__vmm_verb_lock);
  __vmm_verbose = false;
  spinlock_unlock(&__vmm_verb_lock);
}

static void __print_tnode(ttree_node_t *tnode)
{
  int i;
  vmrange_t *vmr;

  tnode_for_each_index(tnode, i) {
    vmr = container_of(tnode_key(tnode, i), vmrange_t, bounds);
    kprintf("[%p<->%p FP: %zd), ", vmr->bounds.space_start,
            vmr->bounds.space_end, (vmr->hole_size >> PAGE_WIDTH));
  }

  kprintf("\n");
}

void vmranges_print_tree_dbg(vmm_t *vmm)
{
  ttree_print(&vmm->vmranges_tree, __print_tnode);
}
#endif /* CONFIG_DEBUG_MM */
