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

#include <ds/iterator.h>
#include <ds/list.h>
#include <mlibc/kprintf.h>
#include <mlibc/stddef.h>
#include <mm/mm.h>
#include <mm/page.h>
#include <mm/mmap.h>
#include <mm/pfalloc.h>
#include <mm/vmm.h>
#include <eza/mutex.h>
#include <eza/errno.h>
#include <eza/arch/page.h>
#include <eza/arch/mm.h>
#include <eza/arch/ptable.h>
#include <eza/arch/types.h>

rpd_t kernel_rpd;

#include <config.h>
#include <ds/list.h>
#include <ds/ttree.h>
#include <mm/page.h>
#include <mm/pfalloc.h>
#include <mm/slab.h>
#include <mm/memobj.h>
#include <mm/vmm.h>
#include <mlibc/kprintf.h>
#include <mlibc/types.h>

static LIST_DEFINE(mandmaps_lst);
static int __mandpas_total = 0;
static memcache_t __vmms_cache = NULL;
static memcache_t __vmrs_cache = NULL;

static int __vmranges_cmp(void *r1, void *r2)
{
  struct range_bounds *range1, *range2;
  long diff;

  range1 = (struct range_bounds *)r1;
  range2 = (struct range_bounds *)r2;
  if ((diff = range1.space_start - range2.space_end) < 0) {
    if ((diff = range2.space_start - range1.space_end) > 0)
      return -1;

    return 0;
  }
  else
    return !!diff;
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
  vmr->bounds.space_end = va_start + (1 << npages);
  vmr->memobj = vmr->private = NULL;
  parent_vmm->num_vmrs++;

  return vmr;
}

void vmm_subsystem_initialize(void)
{
  kprintf("[MM] Initializing VMM subsystem...");
  __vmms_cache = memcache_create("VMM objects cache", sizeof(vmm_t),
                                 DEFAULT_SLAB_PAGES, SMCF_PGEN | SMCF_GENERIC);
  if (!__vmms_cache)
    panic("vmm_subsystem_initialize: Can not create memory cache for VMM objects. ENOMEM");

  __vmrs_cache = memcache_create("Vmrange objects cache", sizeof(vmrange_t),
                                 DEFAULT_SLAB_PAGES, SMCF_PGEN | SMCF_GENERIC);
  if (!__vmrs_cache)
    panic("vmm_subsystem_initialize: Can not create memory cache for vmrange objects. ENOMEM.");
}

void register_mandmap(vm_mandmap_t *mandmap, uintptr_t va_from, uintptr_t va_to, vmrange_flags_t vm_flags)
{
  memset(mandmap, 0, sizeof(mandmap));
  mandmap->bounds.space_start = PAGE_ALIGN_DOWN(va_from);
  mandmap->bounds.space_end = PAGE_ALIGN(va_to);
  mandmap->vmr_flags = flags | VMR_FIXED;
  list_add2tail(&mandmaps_lst, &mandmap->node);
  __mandmaps_total++;
  mandmap->map = mandmap->unmap = NULL;
}

void unregister_manmap(vm_mandmap_t *mandmap)
{
  list_del(&mandmap->node);
  __mandmaps_total--;
}

vmm_t *vmm_create(void)
{
  vmm_t *vmm;

  vmm = alloc_from_memcache(&__vmms_cache);
  if (!vmm)
    return NULL;

  memset(vmm, 0, sizeof(*vmm));
  ttree_init(&vmm->vmranges, __vmranges_cmpf, vmrange_t, bounds);
  atomic_set(&vmm->vmm_users, 1);
  return vmm;
}


uintptr_t find_suitable_vmrange(vmm_t *vmm, uintptr_t length, ttree_iterator_t *ttree_iter)
{
  ttree_iterator_t tti;
  uintptr_t start = USER_START_VIRT;
  struct range_bounds *bounds;

  ttree_iterator_init(vmm->ttree, &tti, NULL, NULL);
  iterate_forward(&tti) {
    bounds = tnode_key(tti.meta_cur.tnode, tti.meta_cur.idx);
    if ((bounds.space_start - start) >= length) {
      if (ttree_iter)
        ttree_iterator_init(&vmm->ttree, ttree_iter, &tti.meta_cur, NULL);

      return bounds.space_start;
    }

    start = bounds.space_end;
  }

  return 0;
}

vmrange_t *vmrange_find(vmm_t *vmm, uintptr_t va_start, uintptr_t va_end, ttree_iterator_t *tti)
{
  if (unlikely(!vmm->num_vmrs)) {    
    if (tti) {
      memset(tti, 0, sizeof(*tti));
      tti->state = ITER_LIE;
    }

    return NULL;
  }
  else {
    struct range_bounds bounds;
    tnode_meta_t meta;
    vmrange_t *vmr;

    bounds.space_start = va_start;
    bounds.space_end = va_end;
    vmr = ttree_lookup(&vmm->vmranges_tree, &bounds, &meta);
    if (tti)
      ttree_iterator_init(&vmm->ttree, tti, meta->tnode, NULL);

    return vmr;
  }
}

long vmrange_map(memobj_t *memobj, vmm_t *vmm, uintptr_t addr, int npages,
                 vmrange_flags_t flags, int offs_pages)
{
  vmrange_t *vmr, *vmr_prev;
  ttree_iterator_t tti;
  status_t err = 0;

  if (!(flags & VMR_PROTO_MASK)
      || !(flags & (VMR_PRIVATE | VMR_SHARED))
      || ((flags & (VMR_PRIVATE | VMR_SHARED)) == (VMR_PRIVATE | VMR_SHARED))
      || !npages
      || ((flags & VMR_NONE) && ((flags & VMR_PROTO_MASK) != VMR_NONE))) {
    err = -EINVAL;
    goto err;
  }
  if (addr) {
    if (!uspace_varange_is_valid(addr, (1 << npages))) {
      if (flags & VMR_FIXED) {
        err = -ENOMEM;
        goto err;
      }
    }
    else {
      vmr_prev = vmrange_find(vmm, addr, addr + (1 << npages), &tti);
      if (vmr_prev) {
        if (flags & VMR_FIXED) {
          err = -EINVAL;
          goto err;
        }
      }

      goto create_vmrange;
    }
  }

  addr = find_suitable_vmrange(vmm, (1 << npages), &tti);
  create_vmrange:
  vmr = create_vmrange(vmm, addr, npages, flags);
  if (!vmr) {
    err = -ENOMEM;
    goto err;
  }
  
  ttree_insert_placeful(&vmm->ttree, &tti->meta_cur, &vmr->bounds);
  if (flags & VMR_POPULATE) {
    page_frame_t *pages;
    
    pages = alloc_pages4uspace(vmm, npages);
    if (!pages) {
      err = -ENOMEM;
      goto err;
    }
    else {
      mmap_info_t minfo;
      page_frame_iterator_t pfi;      
      ITERATOR_CTX(page_frame, PF_ITER_PBLOCK) pblock_ctx;

      pfi_pblock_init(&pfi, &pblock_ctx, list_node_first(&pages->head), 0,
                      list_node_last(&pages->head),
                      pages_block_size(list_entry(list_node_last(&pages->head), page_frame_t, node)));
      iter_first(&pfi);
      minfo.va_from = addr;
      minfo.va_to = addr + (1 << npages);
      minfo.ptable_flags = mpf2ptf(flags);
      minfo.pfi = &pfi;
      err = ptable_map(vmm->rpd, &minfo);
      if (err) {
        ptable_unmap(vmm->rpd, addr, npages);
        goto err;
      }
    }
  }

  return addr;
  
  err:
  if (vmr) {
    vmr->parent_vmm->num_vmrs--;
    ttree_delete_placeful(&vmm->ttree, &tti->meta_cur);
    memfree(vmr);
  }
  
  return err;
}

#define MMAP_KERN_FLAGS_MASK ()

status_t mmap_kern(uintptr_t va, page_idx_t first_page, int npages, kmap_flags_t flags)
{
  mmap_info_t minfo;
  page_frame_iterator_t pfi;
  ITERATOR_CTX(page_frame, PF_ITER_INDEX) pf_idx_ctx;

  minfo.va_from = PAGE_ALIGN_DOWN(va);
  minfo.va_to = minfo.va_from + ((npages - 1) << PAGE_WIDTH);
  minfo.ptable_flags = mpf2ptf(flags & KMAP_FLAGS_MASK);
  pfi_index_init(&pfi, &pf_idx_ctx, first_page, first_page + npages - 1);
  iter_first(&pfi);
  minfo.pfi = &pfi;
  return ptable_map(&kernel_rpd, &minfo);
}
