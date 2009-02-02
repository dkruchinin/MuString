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
#include <eza/spinlock.h>
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

#endif /* CONFING_DEBUG_MM */

static int __vmranges_cmp(void *r1, void *r2)
{
  struct range_bounds *range1, *range2;
  long diff;

  range1 = (struct range_bounds *)r1;
  range2 = (struct range_bounds *)r2;
  if ((diff = range1->space_start - range2->space_end) < 0) {
    if ((diff = range2->space_start - range1->space_end) > 0)
      return -1;

    return 0;
  }
  else
    return !diff;
}

static inline bool __vmranges_can_be_merged(const vmrange_t *vmr1, const vmrange_t *vmr2)
{
  return ((vmr1->flags == vmr2->flags) && (vmr1->memobj == vmr2->memobj));
}

static vmrange_t *create_vmrange(vmm_t *parent_vmm, uintptr_t va_start, int npages, vmrange_flags_t flags)
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
  vmr->hole_size = 0;
  parent_vmm->num_vmrs++;

  return vmr;
}

static void destroy_vmrange(vmrange_t *vmr)
{
  vmr->parent_vmm->num_vmrs--;
  memfree(vmr);
}

#if 0
static void try_merge_vmranges(vmm_t *vmm, vmrange_t *vmrange, tnode_meta_t *meta)
{
  vmrange_t *prev_vmr, *next_vmr;
  tnode_meta_t merg_meta;

  prev_vmr = next_vmr = NULL;

  /* At first try merge with previous VM range */
  memcpy(&merg_meta, meta, sizeof(merg_meta));
  if (!tnode_meta_prev(&vmm->vmranges_tree, &merg_meta)) {
    prev_vmr =
      ttree_key2item(&vmm->vmranges_tree, tnode_key(merg_meta.tnode, merg_meta.idx));
    if (!prev_vmr->hole_size && __vmranges_can_be_merged(vmrange, prev_vmr)) {
      VMM_VERBOSE("%s: Merge intervals [%p,%p) with [%p,%p)\n", vmm_get_name_dbg(vmm),
                  prev_vmr->bounds.space_start, prev_vmr->bounds.space_end,
                  vmrange->bounds.space_start, vmrange->bounds.space_end);
      vmrange->bounds.space_start = prev_vmr->bounds.space_start;
      ttree_delete_placeful(&vmm->vmranges_tree, &merg_meta);
      vmm->num_vmrs--;
    }
    else
      prev_vmr = NULL;
  }

  /* then with right one */
  if (!vmrange->hole_size) {
    memcpy(&merg_meta, meta, sizeof(merg_meta));
    if (!tnode_meta_next(&vmm->vmranges_tree, &merg_meta)) {
      next_vmr =
        ttree_key2item(&vmm->vmranges_tree, tnode_key(merg_meta.tnode, merg_meta.idx));
      if (__vmranges_can_be_merged(vmrange, next_vmr)) {
        VMM_VERBOSE("%s: Merge intervals [%p,%p) with [%p,%p)\n", vmm_get_name_dbg(vmm),
                    vmrange->bounds.space_start, vmrange->bounds.space_end,
                    next_vmr->bounds.space_start, next_vmr->bounds.space_end);
        vmrange->bounds.space_end = next_vmr->bounds.space_start;
        vmrange->hole_size = 0;
        if (!prev_vmr)
          ttree_delete_placeful(&vmm->vmranges_tree, &merg_meta);
        else
          ttree_delete(&vmm->vmranges_tree, &next_vmr->bounds);

        vmm->num_vmrs--;
      }
    }
  }
}
#endif 

static vmrange_t *split_vmrange(vmm_t *vmm, vmrange_t *vmrange, uintptr_t va_from,
                                uintptr_t va_to, ttree_cursor_t *cursor)
{
  vmrange_t *new_vmr;
  ttree_cursor_t csr_next;

  new_vmr = create_vmrange(vmm, va_to, vmrange->bounds.space_end, vmrange->flags);
  if (!new_vmr)
    return NULL;

  VMM_VERBOSE("%s: Split VM range [%p, %p) to [%p, %p) and [%p, %p)\n",
              vmm_get_name_dbg(vmm), vmrange->bounds.space_start, vmrange->bounds.space_end,
              vmrange->bounds.space_start, va_from, va_to, vmrange->bounds.space_end);
  vmrange->bounds.space_end = va_from;
  new_vmr->memobj = vmrange->memobj;
  vmrange->hole_size = new_vmr->bounds.space_start - vmrange->bounds.space_end;
  ttree_cursor_copy(&csr_next, cursor);
  ttree_cursor_next(&csr_next);
  ttree_insert_placeful(&csr_next, new_vmr);

  return new_vmr;
}

static void fix_vmrange_holes(vmm_t *vmm, vmrange_t *vmrange, ttree_cursor_t *cursor)
{
  vmrange_t *vmr;
  ttree_cursor_t csr;

  ttree_cursor_copy(&csr, cursor);
  if (!ttree_cursor_prev(&csr)) {
    vmr = ttree_item_from_cursor(&csr);
    VMM_VERBOSE("%s [%p, %p): old hole size: %p, new hole size: %p\n",
                vmm_get_name_dbg(vmm), vmr->bounds.space_start, vmr->bounds.space_end,
                vmr->hole_size, vmrange->bounds.space_start - vmr->bounds.space_end);
    vmr->hole_size = vmrange->bounds.space_start - vmr->bounds.space_end;
  }

  ttree_cursor_copy(&csr, cursor);
  if (!ttree_cursor_next(&csr)) {
    VMM_VERBOSE("%s [%p, %p): old hole size: %p, new hole size: %p\n",
                vmm_get_name_dbg(vmm), vmrange->bounds.space_start, vmrange->bounds.space_end,
                vmrange->hole_size, vmr->bounds.space_start - vmrange->bounds.space_end);
    vmr = ttree_item_from_cursor(&csr);
    vmrange->hole_size = vmr->bounds.space_start - vmrange->bounds.space_end;
  }
  else {
    VMM_VERBOSE("%s [%p, %p): old hole size: %p, new hole size: %p\n",
                vmm_get_name_dbg(vmm), vmrange->bounds.space_start, vmrange->bounds.space_end,
                vmrange->hole_size, USPACE_VA_TOP - vmrange->bounds.space_end);
    vmrange->hole_size = USPACE_VA_TOP - vmrange->bounds.space_end;
  }
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
    status = mmap_core(&target_mm->rpd, mandmap->virt_addr, mandmap->phys_addr << PAGE_WIDTH,
                       mandmap->num_pages, mandmap->flags);
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
  if (ptable_rpd_initialize(&vmm->rpd) < 0) {
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
                 vmrange_flags_t flags, int offs_pages)
{
  vmrange_t *vmr;
  ttree_cursor_t cursor, csr_tmp;
  int err = 0;

  vmr = NULL;
  ttree_cursor_init(&vmm->vmranges_tree, &cursor);
  if (!(flags & VMR_PROTO_MASK)
      || !(flags & (VMR_PRIVATE | VMR_SHARED))
      || ((flags & (VMR_PRIVATE | VMR_SHARED)) == (VMR_PRIVATE | VMR_SHARED))
      || !npages
      || ((flags & VMR_NONE) && ((flags & VMR_PROTO_MASK) != VMR_NONE))) {
    err = -EINVAL;
    goto err;
  }
  if (addr) {
    if (!valid_user_address_range(addr, (npages << PAGE_WIDTH))) {
      if (flags & VMR_FIXED) {
        err = -ENOMEM;
        goto err;
      }
    }

    vmr = vmrange_find(vmm, addr, addr + (npages << PAGE_WIDTH), &cursor);
    if (vmr) {
      if (flags & VMR_FIXED) {
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
    if ((vmr->flags == flags) && (memobj == vmr->memobj) &&
        vmr->bounds.space_end == addr) {
      VMM_VERBOSE("%s: Attach [%p, %p) to the top of [%p, %p)\n", vmm_get_name_dbg(vmm),
                  addr, addr + (npages << PAGE_WIDTH), vmr->bounds.space_start, vmr->bounds.space_end);
      vmr->hole_size -= (addr + (npages << PAGE_WIDTH) - vmr->bounds.space_end);
      vmr->bounds.space_end = addr + (npages << PAGE_WIDTH);
      goto out;
    }
  }

  ttree_cursor_copy(&csr_tmp, &cursor);
  if (!ttree_cursor_next(&csr_tmp)) {
    vmrange_t *prev = vmr;
    
    vmr = ttree_item_from_cursor(&csr_tmp);
    if ((vmr->flags == flags) && (memobj == vmr->memobj) &&
        (vmr->bounds.space_start == (addr + (npages << PAGE_WIDTH)))) {
      VMM_VERBOSE("%s: Attach [%p, %p) to the bottom of [%p, %p)\n", vmm_get_name_dbg(vmm),
                  addr, addr + (npages << PAGE_WIDTH), vmr->bounds.space_start, vmr->bounds.space_end);
      vmr->bounds.space_start = addr;
      if (prev)
        prev->hole_size = vmr->bounds.space_start - prev->bounds.space_end;

      goto out;
    }
  }

  vmr = create_vmrange(vmm, addr, npages, flags);
  if (!vmr) {
    err = -ENOMEM;
    goto err;
  }

  ttree_insert_placeful(&cursor, vmr);  
  fix_vmrange_holes(vmm, vmr, &cursor);
  
  out:
  return addr;
  
  err:
  if (vmr) {
    ttree_delete_placeful(&cursor);
    destroy_vmrange(vmr);
  }

  return err;
}

void unmap_vmranges(vmm_t *vmm, uintptr_t va_from, page_idx_t npages)
{
  vmrange_set_t vmrs;
  uintptr_t va_to = va_from + (npages << PAGE_WIDTH);

  ASSERT(!(va_from & PAGE_MASK));
  vmranges_find_covered(vmm, va_from, va_from + (npages << PAGE_WIDTH), &vmrs);
  if (!vmrs.vmr)
    return;
  if ((vmrs.vmr->bounds.space_start < va_from) &&
      (vmrs.vmr->bounds.space_end > va_to)) {
    if (!split_vmrange(vmm, vmrs.vmr, va_from, va_to, &vmrs.cursor)) {
      kprintf(KO_ERROR "unmap_vmranges: Failed to split VM range [%p, %p). -ENOMEM\n",
              vmrs.vmr->bounds.space_start, vmrs.vmr->bounds.space_end);
    }

    munmap_core(&vmm->rpd, va_from, npages);
    return;
  }
  else if (va_from > vmrs.vmr->bounds.space_start) {
    vmrs.vmr->hole_size += vmrs.vmr->bounds.space_end - va_from;    
    munmap_core(&vmm->rpd, va_from, vmrs.vmr->bounds.space_end);
    vmrs.vmr->bounds.space_end = va_from;
  }

  vmrange_set_next(&vmrs);
  while (vmrs.vmr) {
    if (unlikely(vmrs.va_to < vmrs.vmr->bounds.space_end)) {
      ttree_cursor_t csr_prev;

      ttree_cursor_copy(&csr_prev, &vmrs.cursor);
      vmrs.vmr->bounds.space_start = vmrs.va_to;
      if (!ttree_cursor_prev(&csr_prev)) {
        vmrange_t *vmr = ttree_item_from_cursor(&csr_prev);
        vmr->hole_size = vmrs.vmr->bounds.space_start - vmr->bounds.space_end;
      }
    }

    munmap_core(&vmm->rpd, vmrs.va_from, ((vmrs.va_to - vmrs.va_from) << PAGE_WIDTH));
    vmrange_set_next(&vmrs);
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
