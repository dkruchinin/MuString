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
#include <mlibc/kprintf.h>
#include <mlibc/types.h>

rpd_t kernel_rpd;
static memcache_t *__vmms_cache = NULL;
static memcache_t *__vmrs_cache = NULL;
static LIST_DEFINE(__mandmaps_lst);
static int __num_mandmaps = 0;

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


static void destroy_vmrange(vmrange_t *vmr)
{
  vmr->parent_vmm->num_vmrs--;
  memfree(vmr);
}

void vm_mandmap_register(vm_mandmap_t *mandmap, const char *mandmap_name)
{
  ASSERT(!valid_user_address_range(mandmap->virt_addr, mandmap->num_pages << PAGE_WIDTH));
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


uintptr_t find_suitable_vmrange(vmm_t *vmm, uintptr_t length, tnode_meta_t *meta)
{
  ttree_iterator_t tti;
  uintptr_t start = USPACE_VA_BOTTOM;
  struct range_bounds *bounds;

  ttree_iterator_init(&vmm->vmranges_tree, &tti, NULL, NULL);
  iterate_forward(&tti) {
    bounds = tnode_key(tti.meta_cur.tnode, tti.meta_cur.idx);
    if ((bounds->space_start - start) >= length) {
      if (meta)
        memcpy(meta, &tti.meta_cur, sizeof(*meta));

      return bounds->space_start;
    }

    start = bounds->space_end;
  }

  return 0;
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

  if (!(flags & VMR_PROTO_MASK)
      || !(flags & (VMR_PRIVATE | VMR_SHARED))
      || ((flags & (VMR_PRIVATE | VMR_SHARED)) == (VMR_PRIVATE | VMR_SHARED))
      || !npages
      || ((flags & VMR_NONE) && ((flags & VMR_PROTO_MASK) != VMR_NONE))) {
    err = -EINVAL;
    goto err;
  }
  if (addr) {
    if (!valid_user_address_range(addr, (1 << npages))) {
      if (flags & VMR_FIXED) {
        err = -ENOMEM;
        goto err;
      }
    }
    else {
      vmr_prev = vmrange_find(vmm, addr, addr + (1 << npages), &tmeta);
      if (vmr_prev) {
        if (flags & VMR_FIXED) {
          err = -EINVAL;
          goto err;
        }
      }

      goto create_vmrange;
    }
  }

  addr = find_suitable_vmrange(vmm, (1 << npages), &tmeta);
  create_vmrange:
  vmr = create_vmrange(vmm, addr, npages, flags);
  if (!vmr) {
    err = -ENOMEM;
    goto err;
  }
  
  ttree_insert_placeful(&vmm->vmranges_tree, &tmeta, &vmr);
  if (flags & VMR_POPULATE) {
    page_frame_t *pages;
    
    pages = alloc_pages4uspace(vmm, npages);
    if (!pages) {
      err = -ENOMEM;
      goto err;
    }
    else {
      /*page_frame_iterator_t pfi;      
      ITERATOR_CTX(page_frame, PF_ITER_PBLOCK) pblock_ctx;

      pfi_pblock_init(&pfi, &pblock_ctx, list_node_first(&pages->head), 0,
                      list_node_last(&pages->head),
                      pages_block_size(list_entry(list_node_last(&pages->head), page_frame_t, node)));
      iter_first(&pfi);
      minfo.va_from = addr;
      minfo.va_to = addr + (1 << npages);
      minfo.ptable_flags = kmap_to_ptable_flags(flags);
      minfo.pfi = &pfi;
      err = ptable_map(&vmm->rpd, &minfo);
      if (err) {
        munmap_core(&vmm->rpd, addr, npages);
        goto err;
        }*/
    }
  }

  return addr;
  
  err:
  if (vmr) {
    ttree_delete_placeful(&vmm->vmranges_tree, &tmeta);
    destroy_vmrange(vmr);
  }
  
  return err;
}

int mmap_core(rpd_t *rpd, uintptr_t va, page_idx_t first_page, ulong_t npages, kmap_flags_t flags)
{
  page_frame_iterator_t pfi;
  ITERATOR_CTX(page_frame, PF_ITER_INDEX) pf_idx_ctx;

  if ((va & PAGE_MASK) || !npages)
    return -EINVAL;

  pfi_index_init(&pfi, &pf_idx_ctx, first_page, first_page + npages - 1);
  iter_first(&pfi);
  return ptable_map(rpd, va, npages, &pfi, kmap_to_ptable_flags(flags & KMAP_FLAGS_MASK));
}
