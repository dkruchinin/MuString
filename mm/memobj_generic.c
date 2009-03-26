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
#include <mm/rmap.h>
#include <mlibc/types.h>

#ifdef CONFIG_DEBUG_GMO
#define GMO_DBG(fmt, args...)                                \
  do {                                                       \
    kprintf("[GMO_DBG] (%s) ", __FUNCTION__);                \
    kprintf(fmt, ##args);                                    \
  } while (0)
#else
#define GMO_DBG(fmt, args...)
#endif /* CONFIG_DEBUG_GMO */

static int __mmap_one_phys_page(vmm_t *vmm, page_idx_t pidx,
                                uintptr_t addr, vmrange_flags_t flags)
{
  int ret;

  if (unlikely(page_is_mapped(&vmm->rpd, addr))) {
    GMO_DBG("(pid %ld) Failed to mmap one physical page %#x to address %p. "
            "Some other page is already mapped by this address.\n",
            vmm->owner->pid, pidx, addr);
    return -EBUSY;
  }
  
  ret = mmap_one_page(&vmm->rpd, addr, pidx, vmr->flags);
  if (ret) {
    GMO_DBG("(pid %ld) Failed to mmap one physical page %#x to address %p. [RET = %d]\n",
            vmm->owner->pid, pidx, addr, ret);
    return ret;
  }
  if (likely(page_idx_is_present(i))) {
    page_frame_t *p = pframe_by_number(i);
    
    pin_page_frame(p);
    p->offset = pidx;
  }

  return ret;
}

static int __mmap_one_anon_page(vmm_t *vmm, page_frame_t *page,
                                uintptr_t addr, vmrange_flags_t flags)
{
  int ret;

  if (unlikely(page_is_mapped(&vmm->rpd, addr))) {
    GMO_DBG("(pid %ld) Failed to mmap one anonymous page %#x to address %p. "
            "Some other page is already mapped by this address.\n",
            vmm->owner->pid, pframe_number(page), addr);
    free_page(page);
    return -EBUSY;
  }
  
  ret = mmap_one_page(&vmm->rpd, addr, pframe_number(page), flags);
  if (ret) {
    GMO_DBG("(pid %ld) Failed to mmap one anonymous page %#x to address %p. [RET = %d]\n",
            vmm->owner->pid, pframe_number(page), addr, ret);
    return ret;
  }

  pin_page_frame(page);
  lock_page_frame(page, PF_LOCK);
  ret = rmap_register_anon(page, vmm, addr);
  unlock_page_frame(page, PF_LOCK);
  if (unlikely(ret)) {
    GMO_DBG("(pid %ld) Failed to register anonymous rmap for just mapped to %p page %#x. [RET = %d]\n",
            vmm->owner->pid, addr, pframe_number(page), ret);
    munmap_one_page(&vmm->rpd, addr);
    unpin_page_frame(page);
  }

  return ret;
}

static int generic_handle_page_fault(vmrange_t *vmr, uintptr_t addr, uint32_t pfmask)
{
  int ret = 0;
  vmm_t *vmm = vmr->parent_vmm;  

  ASSERT_DBG(!(vmr->flags & VMR_PHYS));
  ASSERT_DBG(!(vmr->flags & VMR_NONE));

  if (pfmask & PFLT_NOT_PRESENT) {
    page_frame_t *pf;
    vmrange_flags_t mmap_flags = vmr->flags;

    if (!(pfmask & PFLT_WRITE)) {
      mmap_flags &= ~VMR_WRITE;
    }


    ret = memobj_prepare_page_raw(vmr->memobj, &pf);
    if (ret) {
      return ret;
    }
    
    /*
     * Simple "not-present" fault is handled by allocating a new page and
     * mapping it into the place we've been asked.
     */
    pagetable_lock(&vmm->rpd);
    ret = __mmap_one_anon_page(vmm, pf, addr, mmap_flags);
    if (ret) {
      /* If somebody has handled a fault by given address before us, we have nothing to do. */
      if (ret == -EBUSY)
        ret = 0;

      goto out_unlock;
    }    
  }
  else  {
    pde_t *pde;
    page_frame_t *page;
    page_idx_t pidx;

    pagetable_lock(&vmm->rpd);
    pidx = __vaddr2page_idx(&vmm->rpd, addr, &pde);
    ASSERT(pidx != PAGE_IDX_INVAL);
    if (unlikely(ptable_to_kmap_flags(pde_get_flags(pde)) & KMAP_WRITE)) {
      ret = 0;
      goto out_unlock;
    }

    page = pframe_by_number(pidx);
    if (likely(!(page->flags & PF_COW))) {
      /*
       * Just remap page with rights we've been asked.
       * TODO DK: mark page as dirty.
       */

      ret = mmap_one_page(&vmm->rpd, addr, pidx, vmr->flags);
    }
    else { /* Handle copy-on-write */
      page_frame_t *new_page;
      
      ret = memobj_prepare_page_raw(vmr->memobj, &new_page);
      if (ret) {
        goto out_unlock;
      }

      ret = memobj_handle_cow(vmr, addr, new_page, page);
      
      /*
       * memobj_prepare_page_raw sets page's refcount to 1. If memobj_handle_cow
       * failed, page unpinning will result its freeing,
       * otherwise nothing special will happen.
       */
      unpin_page_frame(new_page);
      if (ret) {
        GMO_DBG("[cow] (pid %ld) Failed to handle COW by address %p for page %#x. [RET = %d]\n",
                vmm->owner->pid, addr, pframe_number(page));        
      }
    }
  }

out_unlock:
  pagetable_unlock(&vmm->rpd);
  return ret;
}

static int generic_populate_pages(vmrange_t *vmr, uintptr_t addr, page_idx_t npages)
{
  int ret;
  memobj_t *memobj = vmr->memobj;    
  vmm_t *vmm = vmr->parent_vmm;
  page_frame_t *p, *pages = NULL;
  
  if (likely(!(vmr->flags & VMR_PHYS))) {
    list_head_t chain_head;

    list_init_head(&chain_head);
    pages = alloc_pages(npages, AF_ZERO | AF_USER);
    if (!pages)
      return -ENOMEM;

    list_set_head(&chain_head, pages);
    pagetable_lock(&vmm->rpd);
    list_for_each_entry(&chain_head, p, chain_node) {
      ret = __mmap_one_anon_page(vmm, p, addr, vmr->flags);
      if (unlikely(ret)) {
        pagetable_unlock(&vmm->rpd);
        return ret;
      }

      p->offset = addr2pgoff(vmr, addr);
      addr += PAGE_SIZE;
    }
    
    pagetable_unlock(&vmm->rpd);
  }
  else {
    page_idx_t idx, i;
    ITERATOR_CTX(page_frame, PF_ITER_INDEX) index_ctx;

    idx = addr2pgoff(vmr, addr);
    pagetable_lock(&vmm->rpd);
    for (i = idx; i < npages; i++, addr += PAGE_SIZE) {
      ret = __mmap_one_phys_page(vmm, i, addr, vmr->flags);
      if (ret) {
        pagetable_unlock(&vmm->lock);
        return ret;
      }
    }

    pagetable_unlock(&vmm->rpd);
  }

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
  .prepare_page_cow = memobj_prepare_page_cow,
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
