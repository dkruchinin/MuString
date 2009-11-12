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
#include <ds/ttree.h>
#include <mm/mem.h>
#include <mm/vmm.h>
#include <mm/memobj.h>
#include <mm/page_alloc.h>
#include <mm/rmap.h>
#include <mstring/task.h>
#include <mstring/types.h>

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
    return ERR(-EBUSY);
  }

  ret = mmap_page(&vmm->rpd, addr, pidx, flags);
  if (ret) {
    GMO_DBG("(pid %ld) Failed to mmap one physical page %#x "
            "to address %p. [RET = %d]\n", vmm->owner->pid, pidx, addr, ret);
    return ret;
  }
  if (likely(page_idx_is_present(pidx))) {
    page_frame_t *p = pframe_by_id(pidx);

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
    return ERR(-EBUSY);
  }

  ret = mmap_page(&vmm->rpd, addr, pframe_number(page), flags);
  if (ret) {
    GMO_DBG("(pid %ld) Failed to mmap one anonymous page "
            "%#x to address %p. [RET = %d]\n", vmm->owner->pid,
            pframe_number(page), addr, ret);
    return ret;
  }

  pin_page_frame(page);
  lock_page_frame(page, PF_LOCK);
  ret = rmap_register_anon(page, vmm, addr);
  unlock_page_frame(page, PF_LOCK);

  if (unlikely(ret)) {
    GMO_DBG("(pid %ld) Failed to register anonymous rmap for "
            "just mapped to %p page %#x. [RET = %d]\n",
            vmm->owner->pid, addr, pframe_number(page), ret);

    munmap_page(&vmm->rpd, addr);
    unpin_page_frame(page);
  }

  return ret;
}

static int generic_handle_page_fault(vmrange_t *vmr, uintptr_t addr,
                                     uint32_t pfmask)
{
  int ret = 0;
  vmm_t *vmm = vmr->parent_vmm;

  if ((vmr->flags & (VMR_PHYS | VMR_NONE))) {
    return ERR(-EINVAL);
  }
  if (pfmask & PFLT_NOT_PRESENT) {
    page_frame_t *pf;
    vmrange_flags_t mmap_flags = vmr->flags;

    if (!(pfmask & PFLT_WRITE)) {
      mmap_flags &= ~VMR_WRITE;
    }

    pf = alloc_page(MMPOOL_USER | AF_ZERO);
    if (!pf) {
      return ERR(-ENOMEM);
    }

    /*
     * For simple not present faults we have to allocate
     * new page and mmap it into address space of faulted process.
     */
    atomic_set(&pf->refcount, 0);
    pf->offset = addr2pgoff(vmr, addr);

    RPD_LOCK_WRITE(&vmm->rpd);
    ret = __mmap_one_anon_page(vmm, pf, addr, mmap_flags);
    if (ret) {
      /*
       * If somebody has handled a fault by given address
       * before us, we have nothing to do.
       */

      GMO_DBG("(pid %ld): Failed to mmap page %#x to %p. [RET = %d]\n",
              vmm->owner->pid, pframe_number(pf), addr);

      if (ret == -EBUSY) {
        ret = 0;
      }

      free_page(pf);
      goto out_unlock;
    }
  }
  else  {
    pde_t *pde;
    page_frame_t *page;
    page_idx_t pidx;

    RPD_LOCK_WRITE(&vmm->rpd);
    pidx = __vaddr_to_pidx(&vmm->rpd, addr, &pde);
    if (unlikely(pidx == PAGE_IDX_INVAL)) {
      ret = -EFAULT;
      goto out_unlock;
    }
    if (unlikely(ptable_to_kmap_flags(pde_get_flags(pde)) & KMAP_WRITE)) {
      ret = 0;
      goto out_unlock;
    }

    page = pframe_by_id(pidx);
    if (likely(!(page->flags & PF_COW))) {
      /*
       * Just remap page with rights we've been asked.
       * TODO DK: mark page as dirty.
       */

      ret = mmap_page(&vmm->rpd, addr, pidx, vmr->flags);
    }
    else { /* Handle copy-on-write */
      page_frame_t *new_page;

      new_page = alloc_page(MMPOOL_USER);
      if (!new_page) {
        ret = -ENOMEM;
        goto out_unlock;
      }

      atomic_set(&new_page->refcount, 1);
      new_page->offset = page->offset;
      copy_page_frame(new_page, page);
      ret = rmap_unregister_mapping(page, vmm, addr);
      if (ret) {
        GMO_DBG("[%s] Failed to unregister mapping of page %#x "
                "from address %p [ERR = %d]\n", vmm_get_name_dbg(vmm),
                pframe_number(page), addr, ret);

        free_page(new_page);
        unpin_page_frame(page);
        goto out_unlock;
      }

      unpin_page_frame(page);
      ret = mmap_page(&vmm->rpd, addr, pframe_number(new_page), vmr->flags);
      if (likely(!ret)) {
        ret = rmap_register_mapping(vmr->memobj, new_page, vmm, addr);
      }
      else {
        free_page(new_page);
      }
    }
  }

out_unlock:
  RPD_UNLOCK_WRITE(&vmm->rpd);
  return ERR(ret);
}

static int generic_populate_pages(vmrange_t *vmr, uintptr_t addr,
                                  page_idx_t npages)
{
  int ret = 0;
  vmm_t *vmm = vmr->parent_vmm;
  page_frame_t *p, *pages = NULL;

  if (likely(!(vmr->flags & VMR_PHYS))) {
    list_head_t chain_head;

    list_init_head(&chain_head);
    pages = alloc_pages(npages, AF_ZERO | MMPOOL_USER);
    if (!pages) {
      return ERR(-ENOMEM);
    }

    list_set_head(&chain_head, &pages->chain_node);
    RPD_LOCK_WRITE(&vmm->rpd);
    list_for_each_entry(&chain_head, p, chain_node) {
      ret = __mmap_one_anon_page(vmm, p, addr, vmr->flags);
      if (unlikely(ret)) {
        GMO_DBG("(pid %ld): Failed to mmap anonymous page %#x to address %p. "
                "[RET = %d]\n", vmm->owner->pid, pframe_number(p), addr, ret);

        RPD_UNLOCK_WRITE(&vmm->rpd);
        return ERR(ret);
      }

      p->offset = addr2pgoff(vmr, addr);
      addr += PAGE_SIZE;
    }

    RPD_UNLOCK_WRITE(&vmm->rpd);
  }
  else {
    page_idx_t idx, i;

    idx = addr2pgoff(vmr, addr);
    RPD_LOCK_WRITE(&vmm->rpd);
    for (i = 0; i < npages; i++, addr += PAGE_SIZE) {
      ret = __mmap_one_phys_page(vmm, idx + i, addr, vmr->flags);
      if (ret) {
        GMO_DBG("(pid %ld): Failed to mmap physical page %#x to address %p. "
                "[RET = %d]\n", i, addr, ret);

        RPD_UNLOCK_WRITE(&vmm->rpd);
        return ERR(ret);
      }
    }

    RPD_UNLOCK_WRITE(&vmm->rpd);
  }

  return ERR(ret);
}

static int generic_insert_page(vmrange_t *vmr, page_frame_t *page,
                               uintptr_t addr, ulong_t mmap_flags)
{
  vmm_t *vmm = vmr->parent_vmm;
  int ret;
  memobj_t *pg_memobj = memobj_from_page(page);

  if ((addr < vmr->bounds.space_start) ||
      (addr >= vmr->bounds.space_end)) {
    return ERR(-EFAULT);
  }
  if (pg_memobj && (pg_memobj != vmr->memobj)) {
    return ERR(-EINVAL);
  }

  ret = mmap_page(&vmm->rpd, addr, pframe_number(page),
                      mmap_flags & KMAP_FLAGS_MASK);
  if (ret) {
    GMO_DBG("[%d] Failed to insert(mmap) page %#x in VM range [%p, %p) by "
            "address %p (VMM: %s). [ERR = %d]\n",
            vmr->memobj->id, pframe_number(page), vmr->bounds.space_start,
            vmr->bounds.space_end, addr, vmm_get_name_dbg(vmm), ret);
    return ERR(ret);
  }

  page->offset = addr2pgoff(vmr, addr);
  return rmap_register_mapping(vmr->memobj, page, vmm, addr);
}

static int generic_delete_page(vmrange_t *vmr, page_frame_t *page)
{
  uintptr_t addr = pgoff2addr(vmr, page->offset);

  ASSERT(vmr->memobj == memobj_from_page(page));
  munmap_page(&vmr->parent_vmm->rpd, addr);
  return rmap_unregister_mapping(page, vmr->parent_vmm, addr);
}

static int generic_depopulate_pages(vmrange_t *vmr, uintptr_t va_from,
                                     uintptr_t va_to)
{
  vmm_t *vmm = vmr->parent_vmm;
  page_idx_t pidx;
  int ret = 0;
  page_frame_t *page;

  ASSERT(vmr->memobj == generic_memobj);

  RPD_LOCK_WRITE(&vmm->rpd);
  while (va_from < va_to) {
    pidx = vaddr_to_pidx(&vmm->rpd, va_from);
    if (pidx == PAGE_IDX_INVAL) {
      goto eof_cycle;
    }

    munmap_page(&vmm->rpd, va_from);
    if (likely(page_idx_is_present(pidx))) {
      page = pframe_by_id(pidx);
      if (!(vmr->flags & VMR_PHYS)) {
        ret = rmap_unregister_mapping(page, vmm, va_from);
        if (ret) {
          goto out;
        }
      }

      unpin_page_frame(page);
    }

eof_cycle:
    va_from += PAGE_SIZE;
  }

out:
  RPD_UNLOCK_WRITE(&vmm->rpd);
  return ret;
}

static memobj_ops_t generic_memobj_ops = {
  .handle_page_fault = generic_handle_page_fault,
  .populate_pages = generic_populate_pages,
  .depopulate_pages = generic_depopulate_pages,
  .insert_page = generic_insert_page,
  .delete_page = generic_delete_page,
  .cleanup = NULL,
  .truncate = NULL,
};

memobj_t *generic_memobj = NULL;

int generic_memobj_initialize(memobj_t *memobj, uint32_t flags)
{
  ASSERT(!generic_memobj);
  ASSERT(memobj->id == GENERIC_MEMOBJ_ID);
  generic_memobj = memobj;
  memobj->mops = &generic_memobj_ops;
  atomic_set(&memobj->users_count, 2); /* Generic memobject is immortal */
  memobj->flags = MMO_FLG_IMMORTAL;

  return 0;
}
