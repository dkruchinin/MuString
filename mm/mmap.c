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
#include <mm/page.h>
#include <mm/pfalloc.h>
#include <mm/slab.h>
#include <mm/memobj.h>
#include <mm/vmm.h>
#include <mm/pfi.h>
#include <eza/spinlock.h>
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
  parent_vmm->num_vmrs++;

  return vmr;
}

static void destroy_vmrange(vmrange_t *vmr)
{
  vmr->parent_vmm->num_vmrs--;
  memfree(vmr);
}

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

static void fix_vmrange_holes(vmm_t *vmm, vmrange_t *target_vmr, tnode_meta_t *meta)
{
  vmrange_t *vmr;
  tnode_meta_t m;

  memcpy(&m, meta, sizeof(m));
  if (!tnode_meta_prev(&vmm->vmranges_tree, &m)) {
    vmr = ttree_key2item(&vmm->vmranges_tree, tnode_key(m.tnode, m.idx));
    vmr->hole_size = target_vmr->bounds.space_start -
      vmr->bounds.space_end;
  }

  memcpy(&m, meta, sizeof(m));
  if (!tnode_meta_next(&vmm->vmranges_tree, &m)) {
    vmr = ttree_key2item(&vmm->vmranges_tree, tnode_key(m.tnode, m.idx));
    target_vmr->hole_size = vmr->bounds.space_start -
      target_vmr->bounds.space_end;
  }
  else
    target_vmr->hole_size = USPACE_VA_TOP - target_vmr->bounds.space_end;
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

uintptr_t find_free_vmrange(vmm_t *vmm, uintptr_t length, tnode_meta_t *meta)
{    
  uintptr_t start = USPACE_VA_BOTTOM;
  vmrange_t *vmr;
  ttree_node_t *tnode = NULL;
  int i = 0;
  bool gonext = false;

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
        gonext = true;
        goto found;
      }
    }

    tnode = tnode->successor;
  }

  return INVALID_ADDRESS;
  found:
  if (meta) {
    tnode_meta_fill(meta, tnode, i, TNODE_BOUND);
    if (gonext)
      tnode_meta_next(&vmm->vmranges_tree, meta);
  }

  return start;
}

vmrange_t *vmrange_find(vmm_t *vmm, uintptr_t va_start, uintptr_t va_end, tnode_meta_t *meta)
{
  if (unlikely(!vmm->num_vmrs))
    return NULL;
  else {
    struct range_bounds bounds;
    vmrange_t *vmr;

    bounds.space_start = va_start;
    bounds.space_end = va_end;
    vmr = ttree_lookup(&vmm->vmranges_tree, &bounds, meta);
    return vmr;
  }
}

long vmrange_map(memobj_t *memobj, vmm_t *vmm, uintptr_t addr, int npages,
                 vmrange_flags_t flags, int offs_pages)
{
  vmrange_t *vmr, *vmr_prev;
  tnode_meta_t tmeta;
  int err = 0;

  vmr = NULL;
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
    else {
      vmr_prev = vmrange_find(vmm, addr, addr + (npages << PAGE_WIDTH), &tmeta);
      if (vmr_prev) {
        if (flags & VMR_FIXED) {
          err = -EINVAL;
          goto err;
        }
      }
      else
        goto create_vmrange;
    }
  }

  addr = find_free_vmrange(vmm, (npages << PAGE_WIDTH), &tmeta);
  if (addr == INVALID_ADDRESS) {
    err = -ENOMEM;
    goto err;
  }

  create_vmrange:
  vmr = create_vmrange(vmm, addr, npages, flags);
  if (!vmr) {
    err = -ENOMEM;
    goto err;
  }

  ttree_insert_placeful(&vmm->vmranges_tree, &tmeta, vmr);
  fix_vmrange_holes(vmm, vmr, &tmeta);
  kprintf("inserted\n");
  if (flags & VMR_POPULATE) {
    page_frame_t *pages;

    pages = alloc_pages_ncont(npages, AF_CLEAR_RC | AF_ZERO | AF_PGEN);
    if (!pages) {
      err = -ENOMEM;
      goto err;
    }
    else {
      page_frame_iterator_t pfi;
      ITERATOR_CTX(page_frame, PF_ITER_PBLOCK) pblock_ctx;

      pfi_pblock_init(&pfi, &pblock_ctx, list_node_first(&pages->head), 0,
                      list_node_last(&pages->head),
                      pages_block_size(list_entry(list_node_last(&pages->head), page_frame_t, node)));
      iter_first(&pfi);
      err = __mmap_core(&vmm->rpd, addr, npages, &pfi, flags);
      if (err)
        goto err;
    }
  }

  try_merge_vmranges(vmm, vmr, &tmeta);
  return addr;
  
  err:
  if (vmr) {
    ttree_delete_placeful(&vmm->vmranges_tree, &tmeta);
    destroy_vmrange(vmr);
  }
  
  return err;
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
            vmr->bounds.space_end, (vmr->hole_size << PAGE_WIDTH));
  }

  kprintf("\n");
}

void vmranges_print_tree_dbg(vmm_t *vmm)
{
  ttree_print(&vmm->vmranges_tree, __print_tnode);
}
#endif /* CONFIG_DEBUG_MM */
