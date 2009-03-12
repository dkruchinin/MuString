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

static int generic_handle_page_fault(vmrange_t *vmr, uintptr_t addr, uint32_t pfmask)
{
  int ret = 0;
  vmm_t *vmm = vmr->parent_vmm;

  ASSERT(!(vmr->flags & VMR_PHYS));
  ASSERT(!(vmr->flags & VMR_NONE));
  if (pfmask & PFLT_NOT_PRESENT) {
    page_frame_t *pf;

    pf = alloc_page(AF_USER | AF_ZERO);
    if (!pf) {
      ret = -ENOMEM;
      goto out;
    }

    /*
     * Simple "not-present" fault is handled by allocating a new page and
     * mapping it into the place we've been asked.
     */
    pagetable_lock(&vmm->rpd);
    if (unlikely(ptable_ops.vaddr2page_idx(&vmm->rpd, addr, NULL) != PAGE_IDX_INVAL)) {
      /*
       * In very rare situatinons several threads may generate a fault by one address.
       * So here we have to check if the fault was handled by somebody earlier. If so,
       * our job is done.
       */
      pagetable_unlock(&vmm->rpd);
      free_page(pf);
      goto out;
    }

    ret = mmap_core(&vmm->rpd, addr, pframe_number(pf),
                    1, vmr->flags & KMAP_FLAGS_MASK, true);
    pagetable_unlock(&vmm->rpd);    
    if (ret)
      free_page(pf);
  }
  else  {
    pde_t *pde;
    page_frame_t *page;
    page_idx_t pidx;
    
    pagetable_lock(&vmm->rpd);
    pidx = ptable_ops.vaddr2page_idx(&vmm->rpd, addr, &pde);
    ASSERT(pidx != PAGE_IDX_INVAL);
    if (unlikely(ptable_to_kmap_flags(pde_get_flags(pde)) & KMAP_WRITE)) {
      pagetable_unlock(&vmm->rpd);
      goto out;
    }

    page = pframe_by_number(pidx);
    if (page->flags & PF_COW) {
      page_frame_t *new_page = alloc_page(AF_USER);

      if (!new_page) {
        pagetable_unlock(&vmm->rpd);
        ret = -ENOMEM;
        goto out;
      }

      copy_page_frame(new_page, page);
      unpin_page_frame(page);
      page = new_page;
    }

    ret = mmap_core(&vmm->rpd, addr, pframe_number(page), 1,
                    vmr->flags & KMAP_FLAGS_MASK, true);
    pagetable_unlock(&vmm->rpd);
  }

  out:
  return ret;
}

static int generic_populate_pages(vmrange_t *vmr, uintptr_t addr, page_idx_t npages)
{
  int ret;
  memobj_t *memobj = vmr->memobj;    
  page_frame_iterator_t pfi;
  vmm_t *vmm = vmr->parent_vmm;
  page_frame_t *p, *pages = NULL;
  pgoff_t offs;
  
  if (!(vmr->flags & VMR_PHYS)) {
    ITERATOR_CTX(page_frame, PF_ITER_LIST) list_ctx;
    
    pages = alloc_pages(npages, AF_ZERO | AF_USER);
    if (!pages)
      return -ENOMEM;

    pfi_list_init(&pfi, &list_ctx, &pages->chain_node, pages->chain_node.prev);
  }
  else {
    page_idx_t idx;
    ITERATOR_CTX(page_frame, PF_ITER_INDEX) index_ctx;

    idx = addr2pgoff(vmr, addr);
    pfi_index_init(&pfi, &index_ctx, idx, idx + npages - 1);
  }

  offs = vmr->offset;
  iterate_forward(&pfi) {
    if (likely(page_idx_is_present(pfi.pf_idx))) {
      p = pframe_by_number(pfi.pf_idx);
      p->offset = offs;
      p->owner = memobj;
      pin_page_frame(p);
    }
    
    offs++;
  }

  iter_first(&pfi);
  pagetable_lock(&vmm->rpd);
  ret = __mmap_core(&vmr->parent_vmm->rpd, addr, npages, &pfi,
                    vmr->flags & KMAP_FLAGS_MASK, false);
  pagetable_unlock(&vmm->rpd);
  if (ret)
    free_pages_chain(pages);

  return ret;
}

static int generic_put_page(memobj_t *memobj, pgoff_t offset, page_frame_t *page)
{
  return -ENOTSUP;
}

static int generic_get_page(memobj_t *memobj, pgoff_t offset, page_frame_t **page)
{
  return -ENOTSUP;
}

static void generic_cleanup(memobj_t *memobj)
{
  panic("Detected an attemption to free generic memory object!");
}

static memobj_ops_t generic_memobj_ops = {
  .handle_page_fault = generic_handle_page_fault,
  .populate_pages = generic_populate_pages,
  .put_page = generic_put_page,
  .get_page = generic_get_page,
  .cleanup = generic_cleanup,
};

memobj_t *generic_memobj = NULL;

int generic_memobj_initialize(memobj_t *memobj, uint32_t flags)
{
  ASSERT(!generic_memobj);
  ASSERT(memobj->id == GENERIC_MEMOBJ_ID);
  generic_memobj = memobj;
  memobj->mops = &generic_memobj_ops;
  atomic_set(&memobj->users_count, 2); /* Generic memobject is immortal */
  memobj->flags = MMO_FLG_NOSHARED | MMO_FLG_IMMORTAL;

  return 0;
}
