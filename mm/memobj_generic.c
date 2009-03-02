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
 * mm/memobj_generic.c - Generic memory object implementation.
 *
 */

#include <config.h>
#include <ds/list.h>
#include <ds/hat.h>
#include <mm/vmm.h>
#include <mm/memobj.h>
#include <mm/pfalloc.h>
#include <mm/ptable.h>
#include <mlibc/types.h>

/* TODO DK: implement generic pages caching */

static int generic_handle_page_fault(vmrange_t *vmr, pgoff_t offset, uint32_t pfmask)
{
  uintptr_t addr = vmr->bounds.space_start + (offset << PAGE_WIDTH);
  int ret = 0;
  vmm_t *vmm = vmr->parent_vmm;
  
  if (offset >= memobj->size)
    return -ENXIO;
  if (pfmask & PFLT_NOT_PRESENT) {
    page_frame_t *pf;
    page_idx_t idx;

    /*
     * Simple "not-present" fault is handled by allocating a new page and
     * mapping it into the place we've been asked.
     */
    pagetable_lock(&vmm->rpd);
    if (unlikely(ptable_ops.vaddr2page_idx(&vmm->rpd, addr, NULL) == PAGE_IDX_INVAL)) {
      /*
       * In very rare situatinons several threads may generate a fault by one address.
       * So here we have to check if the fault was handled by somebody earlier. If so,
       * our job is done.
       */
      pagetable_unlock(&vmm->rpd);
      return 0;
    }
    
    pf = alloc_page(AF_USER | AF_ZERO);
    if (!pf) {
      pagetable_unlock(&vmm->rpd);
      return -ENOMEM;
    }

    ret = mmap_core(&vmm->rpd, addr,
                    pframe_number(pf), 1, vmr->flags & VMR_PROTO_MASK);
    pagetable_unlock(&vmm->rpd);
    if (ret)
      free_page(pf);
  }
  else  { /* protection fault */
    pde_t *pde = NULL;

    /*
     * FIXME DK: do I really need handle the cases when RW page was mapped
     * with RO priviledges? And what about COW?
     */
    ASSERT(!(pfmask & PFLT_WRITE)); /* Debug */
    pagetable_lock(&vmm->rpd);
    ptable_ops.vaddr2page_idx(&vmm->rpd, addr, &pde);
    ASSERT(pde != NULL);
    if (unlikely(ptable_to_kmap_flags(pde->flags) & VMR_WRITE)) {
      pagetable_unlock(&vmm->rpd);
      return 0;
    }

    /* TODO DK: ... and what about caching, a? */
    ret = mmap_core(&vmm->rpd, addr, idx, 1, vmr->flags & VMR_PROTO_MASK);
    pagetable_unlock();
  }

  return ret;
}

static int generic_populate_pages(memobj_t *memobj, vmrange_t *vmr, pgoff_t offset, page_idx_t npages)
{
  int ret;
  page_frame_t *pages;
  page_frame_iterator_t pfi;  
  
  ASSERT(memobj->id == NULL_MEMOBJ_ID);
  ASSERT(vmr->parent_vmm != NULL);
  if (offset >= memobj->size)
    return -ENXIO;
  if (!(vmr->flags & VMR_PHYS)) {
    ITERATOR_CTX(page_frame, PF_ITER_LIST) list_ctx;
    pages = alloc_pages(npages, AF_ZERO | AF_USER);
    if (!pages)
      return -ENOMEM;

    pfi_list_init(&pfi, &list_ctx, &pages->chain_node, pages->chain_node.prev);    
  }
  else {
    ITERATOR_CTX(page_frame, PF_ITER_INDEX) index_ctx;
    pfi_index_init(&pfi, &index_ctx, offset, offset + npages - 1);    
  }

  iter_first(&pfi);
  pagetable_lock(&vmm->rpd);  
  ret = __mmap_core(&vmr->parent_vmm->rpd, (offset << PAGE_WIDTH), npages, &pfi, vmr->flags & KMAP_FLAGS_MASK);
  pagetable_unlock(&vmm->rpd);
  if (ret)
    free_pages_chain(pages);

  return ret;
}

static int generic_put_page(memobj_t *memobj, pgoff_t offset, page_frame_t *page)
{
  return 0;
}

static page_frame_t *generic_get_page(memobj_t *memobj, pgoff_t offset)
{
  return NULL;
}

static memobj_ops_t generic_memobj_ops = {
  .handle_page_fault = generic_handle_page_fault;
  .populate_pages = generic_populate_pages;
  .put_page = generic_put_page;
  .get_page = generic_get_page;
};

memobj_t *generic_memobj = NULL;

int generic_memobj_initialize(memobj_t *memobj, uint32_t flags)
{
  ASSERT(!generic_memobj);
  generic_memobj = memobj;
  memobj->id = GENERIC_MEMOBJ_ID;
  memobj->mops = generic_memobj_ops;
  atomic_set(&memobj->refcount, 1); /* Generic memobject is immortal */
  ASSERT(!flags || ((flags & MMO_FLG_NOSHARED) == MMO_FLG_NOSHARED));
  memobj->flags = MMO_FLG_NOSHARED;

  return 0;
}
