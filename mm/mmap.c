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

    kprintf("0?\n");
    return 0;
  }
  else {
    kprintf("%p -> %p\n", range1->space_start, range1->space_end);
    return !diff;
  }
}

static inline bool __vmranges_can_be_merged(const vmrange_t *vmr1, const vmrange_t *vmr2)
{
  return ((vmr1->flags == vmr2->flags) && (vmr1->memobj == vmr2->memobj));
}

static vmrange_t *previous_vmrange(ttree_t *ttree, tnode_meta_t *tnode_meta, tnode_meta_t *prev_meta)
{
  vmrange_t *vmr = NULL;

  if (tnode_meta->idx > tnode_meta->tnode->min_idx) {
    kprintf("YEAH!\n");
    vmr = ttree_key2item(ttree, tnode_key(tnode_meta->tnode, tnode_meta->idx - 1));
    if (prev_meta) {
      prev_meta->idx = tnode_meta->idx - 1;
      prev_meta->tnode = tnode_meta->tnode;
      prev_meta->side = TNODE_BOUND;
    }
  }
  else {
    ttree_node_t *n = ttree_tnode_glb(tnode_meta->tnode);

    kprintf("YEAH?? %p\n", n);
    if (n) {
      vmr = ttree_key2item(ttree, tnode_key(n, n->max_idx));
      if (prev_meta) {
        prev_meta->idx = n->max_idx;
        prev_meta->tnode = n;
        prev_meta->side = TNODE_BOUND;
      }
    }
  }

  return vmr;
}

static vmrange_t *next_vmrange(ttree_t *ttree, tnode_meta_t *meta, tnode_meta_t *next_meta)
{
  vmrange_t *vmr = NULL;

  if (meta->idx < meta->tnode->max_idx) {
    vmr = ttree_key2item(ttree, tnode_key(meta->tnode, meta->idx + 1));
    if (next_meta) {
      next_meta->idx = meta->idx + 1;
      next_meta->tnode = meta->tnode;
      next_meta->side = TNODE_BOUND;
    }
  }
  else {
    ttree_node_t *n = meta->tnode->successor;

    if (n) {
      vmr = ttree_key2item(ttree, tnode_key(n, n->min_idx));
      if (next_meta) {
        next_meta->idx = n->min_idx;
        next_meta->tnode = n;
        next_meta->side = TNODE_BOUND;
      }
    }
  }

  return vmr;
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
  vmr->memobj = vmr->private = NULL;
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
  vmrange_t *mergeable_vmr = NULL;
  tnode_meta_t mergeable_meta;
  int i;

  /* FIXME DK: among flags and memory object check if two backends are equal(BTW: are backends and memobjs the same?) */
  for (i = 0; i < 2; i++) {
    if (!i)
      mergeable_vmr = previous_vmrange(&vmm->vmranges_tree, meta, &mergeable_meta);
    else
      mergeable_vmr = next_vmrange(&vmm->vmranges_tree, meta, &mergeable_meta);
    if (!mergeable_vmr) {
      kprintf("%d: here\n", i);
      continue;
    }
    if (!__vmranges_can_be_merged(vmrange, mergeable_vmr))
      continue;

    ttree_delete_placeful(&vmm->vmranges_tree, &mergeable_meta);
    kprintf("search: %p, %p\n", vmrange->bounds.space_start, vmrange->bounds.space_end);
    ASSERT(!ttree_replace(&vmm->vmranges_tree, &vmrange->bounds, vmrange));
    ttree_print(&vmm->vmranges_tree);
    if (!i) {
      vmrange->bounds.space_start = mergeable_vmr->bounds.space_start;
      VMM_VERBOSE("Merge ranges [%p, %p) => [%p %p)\n",
                  mergeable_vmr->bounds.space_start, mergeable_vmr->bounds.space_end,
                  vmrange->bounds.space_start, vmrange->bounds.space_start);
    }
    else {
      VMM_VERBOSE("Merge ranges [%p, %p) => [%p %p)\n",        
                  vmrange->bounds.space_start, vmrange->bounds.space_start,
                  mergeable_vmr->bounds.space_start, mergeable_vmr->bounds.space_end);
      vmrange->bounds.space_end = mergeable_vmr->bounds.space_end;
    }
        
    vmm->num_vmrs--;
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

/*
 * TODO DK: optimize
 * Actually the algorithm is quite stupid. It iterates through T*-tree nodes: from
 * the lefmost one and up to the node's successor. So strictly speaking it's just an
 * iteration through a *linked list* which takes O(N). I think it's quite unefficient
 * way to find needful amount of address space. The algorithm may be optimized by
 * using some data structure - parallel with T*-tree - that will store address-space
 * holes sorted by their size in memory-efficient way. May be some t*-tree hooks will
 * be introduced for this purpose. Time'll show (:
 */
uintptr_t find_suitable_vmrange(vmm_t *vmm, uintptr_t length, tnode_meta_t *meta)
{  
  ttree_node_t *tnode, *p;
  uintptr_t start = USPACE_VA_BOTTOM;
  struct range_bounds *bounds;
  int i = 0;
  
  p = tnode = ttree_tnode_leftmost(vmm->vmranges_tree.root);
  while (tnode) {
    p = tnode;
    tnode_for_each_key(tnode, i) {
      bounds = ttree_key2item(&vmm->vmranges_tree, tnode_key(tnode, i));
      kprintf("[%d] %p <-> %p\n", i, bounds->space_start, bounds->space_end);
      if ((bounds->space_start - start) >= length) {
        kprintf("found: %p\n", bounds->space_start);
        start = bounds->space_start;
        goto out;
      }

      start = bounds->space_end;
    }

    tnode = tnode->successor;
  }
  if (unlikely((start + length) >= USPACE_VA_TOP))
    return INVALID_ADDRESS;

  out:
  if (meta && p) {
    meta->tnode = p;
    if (unlikely(tnode_is_full(&vmm->vmranges_tree, p)
                 && (i > p->max_idx))) {
      meta->side = TNODE_RIGHT;
      meta->idx = first_tnode_idx(&vmm->vmranges_tree);
    }
    else {
      meta->side = TNODE_BOUND;
      meta->idx = i;
    }
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
      
      goto create_vmrange;
    }
  }

  addr = find_suitable_vmrange(vmm, (npages << PAGE_WIDTH), &tmeta);
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
  ttree_print(&vmm->vmranges_tree);
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
  kprintf("-> %p\n", addr);
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
#endif /* CONFIG_DEBUG_MM */
