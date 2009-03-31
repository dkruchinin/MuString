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
 * (c) Copyright 2009 Dan Kruchinin <dan.kruchinin@gmail.com>
 *
 * mm/memobj_pcacge.c - Pagecached memory object implementation
 *
 */

#include <config.h>
#include <ds/hat.h>
#include <mm/page.h>
#include <mm/vmm.h>
#include <mm/slab.h>
#include <mm/mem.h>
#include <mm/pfalloc.h>
#include <mm/memobj.h>
#include <mm/rmap.h>
#include <eza/spinlock.h>
#include <mlibc/types.h>

#ifndef CONFIG_DEBUG_PCACHE
#define PCACHE_DBG(fmt, args...)                        \
  do {                                                  \
    kprintf("[PCACHE_DBG (fn: %s) ", __FUNCTION__);     \
    kprintf(fmt, ##args);                               \
  } while (0)
#else
#define PCACHE_DBG(fmt, args...)
#endif /* CONFIG_DEBUG_PCACHE */

struct pcache {
  hat_t pagecache;
  rwsem_t rwlock;
  list_head_t dirty_pages;
  uint32_t num_dirty_pages;
};

static memcache_t *pcache_memcache = NULL;

static inline void __add_dirty_page(vmrange_t *vmr, page_frame_t *page)
{
#if 0
  if (!(memobj->flags & MMO_FLG_DPC) ) {
    /* TODO DK: simply mark page as dirty and put it in LRU or something... */
    return;
  }

  if (atomic_test_and_set_bit(&page->flags, pow2(PF_DIRTY)))
    return; /* Page is already marked as dirty. */

  PAGECACHE_DBG("(pid %ld): New dirty page %#x by offset %#x\n",
                current_task()->pid, pframe_number(page), page->offset);
  list_add2tail(&priv->dirty_pages, &page->node);
  priv->num_dirty_pages++;
#endif 
}

static inline int __mmap_page_paranoic(memobj_t *memobj, vmm_t *vmm, page_frame_t *page,
                                       uintptr_t addr, vmrange_flags_t mmap_flags)
{
  int ret = 0;

  pagetable_lock(&vmm->rpd);
  if (page_is_mapped(&vmm->rpd, addr)) {
    PCACHE_DBG("#PF: some page on address %p is already mapped.\n", addr);
    unpin_page_frame(page);
    goto out;
  }

  ret = mmap_one_page(&vmm->rpd, addr, pframe_number(page), mmap_flags);
  if (!ret) {
    PCACHE_DBG("#PF handled on %p: page %#x, offset %#x\n",
               addr, pframe_number(page), page->offset);
    ret = rmap_register_mapping(memobj, page, vmm, addr);
    if (ret)
      unpin_page_frame(page);
  }

out:
  pagetable_unlock(&vmm->rpd);
  return ret;
}

/* This function expects that page is already pinned */
static int __mmap_cached_page(vmrange_t *vmr, uintptr_t addr,
                              page_frame_t *page, vmrange_flags_t mmap_flags)
{
  vmm_t *vmm = vmr->parent_vmm;

  if (mmap_flags & VMR_WRITE) {
    /*
     * Write fault in private mapping caused #PF.
     * Just create a copy of cached page and mmap it
     * into faulted process address space.
     */
    if (unlikely(vmr->flags & VMR_PRIVATE)) {
      page_frame_t *new_page = alloc_page(AF_USER);

      if (!new_page)
        return -ENOMEM;
      
      copy_page_frame(new_page, page);
      pin_page_frame(new_page);
      new_page->offset = page->offset;
      unpin_page_frame(page);
      page = new_page;
    }

    //__add_dirty_page(memobj, page);
  }
  else {
    mmap_flags &= ~VMR_WRITE;
  }

  return __mmap_page_paranoic(vmr->memobj, vmm, page, addr, mmap_flags);
}

#if 0
static int prepare_backended_page(memobj_t *memobj, pgoff_t offset, uint32_t pfmask, page_frame_t **page)
{
  task_t *server_task;
  ipc_channel_t *channel;
  struct memobj_remote_request req;
  struct memobj_remote_answer answ;
  struct pcache_private *priv;  
  iovec_t snd_iovec, rcv_iovec;
  int ret;

  ASSERT(memobj->backend != NULL);
  memset(&req, 0, sizeof(req));
  req.memobj_id = memobj->id;
  req.pg_offset = offset;
  req.type = MREQ_TYPE_GETPAGE;
  req.fault_mask = MFAULT_NP | MFAULT_READ;
  req.fault_mask |= !!(pfmask & PFLT_WRITE) << pow2(MFAULT_WRITE);
  
  memset(&answ, 0, sizeof(answ));

  snd_iovec.iov_base = (void *)&req;
  snd_iovec.iov_len = sizeof(req);
  rcv_iovec.iov_base = (void *)&answ;
  rcv_iovec.iov_len = sizeof(answ);

  /*
   * FIXME DK: here *must* be a fucking mutex insted of RW spinlock
   * and it *must* be locked until server relies us.
   */
  spinlock_lock_read(&memobj->backend->rwlock);
  server_task = memobj->backend->server;
  grab_task_struct(server_task);
  channel = memobj->backend->channel;
  pin_channel(channel);
  spinlock_unlock_read(&memobj->backend->rwlock);

  PAGECACHE_DBG("(pid %ld) Ask the server (%ld) to give me a page by offset %#x\n",
                current_task()->pid, server->pid, offset);
  ret = ipc_port_send_iov(channel, &snd_iovec, 1, &rcv_iovec, 1);
  if (ret < 0)
    goto out;
  if (answ.status)
    goto out;


  priv = memobj->priv;
  spinlock_lock_read(&priv->cache_lock);
  *page = hat_lookup(&priv->pagecache, offset);
  if (*page) {
    /*
     * If server successfully allocated a new page for a cache by given offset, it(page)
     * must be already inserted in hashed array tree with refcount equal to 1(at least).
     * So here, we have to pin the page to prevent it explicit freeing while we're working with it.
     */
    pin_page_frane(*page);
    PAGECACHE_DBG("(pid %ld) Server (%ld) allocated page %#x for offset %#x\n",
                  current_task()->pid, server->pid, pframe_number(*page), offset);
  }
  else {
    PAGECACHE_DBG("(pid %ld). Server (%ld) failed to allocate new page by offset %#x\n",
                  current_task->pid, server->pid, offset);
    ret = -EFAULT;
  }
  spinlock_unlock_read(&priv->cache_lock);

out:
  release_task_struct(server_task);
  unpin_channel(channel);
  return ret;
}
#endif

static int handle_not_present_fault(vmrange_t *vmr, uintptr_t addr, uint32_t pfmask)
{
  vmm_t *vmm = vmr->parent_vmm;
  memobj_t *memobj = vmr->memobj;
  vmrange_flags_t mmap_flags = vmr->flags;
  pgoff_t offset = addr2pgoff(vmr, addr);
  struct pcache *pcache = memobj->private;
  page_frame_t *page;
  int ret;

  if (!(pfmask & PFLT_WRITE))
    mmap_flags &= ~VMR_WRITE;

  /*
   * With "page cache" memory object there might be exactly
   * two possible situations when page is not present:
   *  1) It is already in a cache(HAT). In this case we simply
   *     mmap it to address space of faulted process
   *    (of course after all flags are checked)
   *  2) It is not in a cache. In this case new page has to be
   *     allocated(implicitly inkernel or explicitly by backend.
   *     It depends on the nature of given object) and inserted in
   *     cache by given offset.
   */

  rwsem_down_read(&pcache->rwlock);
  page = hat_lookup(&pcache->pagecache, offset);
  if (page) {
    PCACHE_DBG("#PF: page by offset %ld[idx = %#x] is already in a cache.\n",
               offset, pframe_number(page));
    /* We don't want page to be fried while we're working with it. */
    pin_page_frame(page);
    ret = __mmap_cached_page(vmr, addr, page, mmap_flags);
    rwsem_up_read(&pcache->rwlock);
    if (ret)
      unpin_page_frame(page);

    return ret;
  }

  rwsem_up_read(&pcache->rwlock);

  if (!page) {
    if (!(memobj->flags & MMO_FLG_BACKENDED)) {
      page = alloc_page(AF_USER | AF_ZERO);
      if (!page) {
        PCACHE_DBG("Failed to allocate page while handling #PF by offset"
                   " %#x and address %p.\n", offset, addr);
        return -ENOMEM;
      }

      atomic_set(&page->refcount, 1);
      page->offset = offset;
      if (unlikely((vmr->flags & VMR_PRIVATE) && (pfmask & PFLT_WRITE))) {
        /*
         * Detected an attemption to write in page that doesn't exist
         * in private mapping. In this case, allocated earlier page will
         * be mapped in address space of faulted precess without insertion
         * of the page in page cache.
         */

        ret = __mmap_page_paranoic(memobj, vmm, page, addr, vmr->flags);
        PCACHE_DBG("Handled #PF on write in private mapping: [offs = %#x, page = #%x, addr = %p]/\n",
                   offset, pframe_number(page), addr);
        /* TODO DK: page *must* be marked as dirty */
        return ret;
      }
      
      /*
       * If page is not belong to private mapping *and* if fault occured
       * on read, allocated page must be inserted in a cache.
       */
      page->flags |= PF_SHARED;
      rwsem_down_write(&pcache->rwlock);
      ret = hat_insert(&pcache->pagecache, offset, page);
      if (ret == -EEXIST) {
        /*
         * While we were allocating page, somebody already
         * inserted another one by the same offset. In simplest
         * case when our mapping is not private, we have to mmap
         * a page from cache, otherwise(if we have a private mapping),
         * we'll copy a page and then mmap its copy.
         */
        PCACHE_DBG("#PF. Failed to insert page %#x(offs = %#x) in a cache. [-EEXIST]\n",
                   pframe_number(page), offset);
        if (likely(!(vmr->flags & VMR_PRIVATE))) {
          unpin_page_frame(page);
          page = hat_lookup(&pcache->pagecache, offset);
          ASSERT(page != NULL);            
        }
        else {
          page_frame_t *new_page = page;

          page = hat_lookup(&pcache->pagecache, offset);
          ASSERT_DBG(page != NULL);
          
          new_page->flags &= ~PF_SHARED;
          copy_page_frame(new_page, page);
        }
      }
      else {
        PCACHE_DBG("#PF. Failed to insert page %#x in cache by offset %#x. [RET = %d]\n",
                   pframe_number(page), offset, ret);
        rwsem_up_write(&pcache->rwlock);
        return ret;
      }

      PCACHE_DBG("#PF. New page %#x was inserted in a cache by offset %#x\n",
                 pframe_number(page), offset);
      ret = __mmap_page_paranoic(memobj, vmm, page, addr, mmap_flags);
      rwsem_up_write(&pcache->rwlock);
    }
    else {
      rwsem_down_write(&pcache->rwlock);
      //ret = prepare_backended_page(memobj, offset, pfmask, &page);
      if (ret) {
        rwsem_up_write(&pcache->rwlock);
        return ret;
      }

      /*
       * If a backed was asked to allocate a page, it must be
       * already in a cache.
       */
      ret = __mmap_cached_page(vmr, addr, page, mmap_flags);
      rwsem_up_write(&pcache->rwlock);
    }
  }

  return ret;
}

static int pcache_handle_page_fault(vmrange_t *vmr,
                                    uintptr_t addr, uint32_t pfmask)
{
  memobj_t *memobj = vmr->memobj;
  pgoff_t offset = addr2pgoff(vmr, addr);
  int ret = 0;

  if ((vmr->flags & VMR_NONE))
    return -EFAULT;
  if (unlikely(offset >= memobj->size)) {
    PCACHE_DBG("handlig #PF on %p by invalid offset %#x (max = %#x)!\n",
               addr, offset, memobj->size);
    return -ENXIO;
  }
  if (unlikely((memobj->flags & MMO_FLG_BACKENDED) &&
               !memobj_has_backend(memobj))) {
    PCACHE_DBG("handling #PF on %p: memory object #%d "
               "has MMO_FLG_BACKENDED but has not backend "
               "itself!\n", addr, memobj->id);
    return -EINVAL;
  }

  if (pfmask & PFLT_NOT_PRESENT) {
    ret = handle_not_present_fault(vmr, addr, pfmask);
  }
  else {
    struct pcache *pcache = memobj->private;
    page_frame_t *page;
    vmm_t *vmm = vmr->parent_vmm;
    page_idx_t pidx;
    pde_t *pde;
    
    pagetable_lock(&vmm->rpd);
    pidx = __vaddr2page_idx(&vmm->rpd, addr, &pde);
    if (unlikely(pidx == PAGE_IDX_INVAL)) {
      /*
       * It's possible that other thread of a process has unmapped
       * a page we faulted on. In this case we can try to populate
       * new page by given address;
       */
      pagetable_unlock(&vmm->rpd);
      PCACHE_DBG("#PF on write: Page by address %p is not present in "
                 "address space of faulted process. Try to populate it.\n", addr);
      ret = handle_not_present_fault(vmr, addr, pfmask);
    }
    else {
      if (unlikely(ptable_to_kmap_flags(pde_get_flags(pde)) & KMAP_WRITE)) {
        /* Somebody already remapped page */        
        pagetable_unlock(&vmm->rpd);
        return 0;
      }

      page = pframe_by_number(pidx);
      if (likely(!(vmr->flags & VMR_PRIVATE))) {
        rwsem_down_read(&pcache->rwlock);
        ASSERT_DBG(hat_lookup(&pcache->pagecache, offset) != NULL);
        
        ret = mmap_one_page(&vmm->rpd, addr, pframe_number(page), vmr->flags);
        if (!ret) {
          lock_page_frame(page, PF_LOCK);
          ret = rmap_register_shared(memobj, page, vmm, addr);
          unlock_page_frame(page, PF_LOCK);
        }

        PCACHE_DBG("#PF on write: Remap shared page %#x(offs = %#x) to address %p. [RET = %d]\n",
                   pframe_number(page), page->offset, addr, ret);
        pagetable_unlock(&vmm->rpd);
        rwsem_up_read(&pcache->rwlock);
      }
      else {
        /*
         * Page is located in private mapping and *may be*
         * has PF_COW flag set.
         */

        page_frame_t *new_page = alloc_page(AF_USER);

        if (!new_page) {
          pagetable_unlock(&vmm->rpd);
          return -ENOMEM;
        }

        copy_page_frame(new_page, page);
        pin_page_frame(new_page);
        new_page->offset = page->offset;
        rmap_unregister_shared(page, vmm, addr);
        unpin_page_frame(page);

        ret = mmap_one_page(&vmm->rpd, addr, pframe_number(new_page), vmr->flags);
        if (!ret) {
          lock_page_frame(new_page, PF_LOCK);
          ret = rmap_register_anon(new_page, vmm, addr);
          unlock_page_frame(new_page, PF_LOCK);
        }
        
        PCACHE_DBG("#PF on write [COW]: New page %#x, addr = %p, offs = %#x. [RET = %p]\n",
                   pframe_number(new_page), addr, offset, ret);

      }
    }

    pagetable_unlock(&vmm->rpd);
  }

  return ret;
}

static memobj_ops_t pcache_ops = {
  .handle_page_fault = pcache_handle_page_fault,
  .populate_pages = NULL,
  .truncate = NULL,
  .cleanup = NULL,
};

int pagecache_memobj_initialize(memobj_t *memobj, uint32_t flags)
{
  struct pcache *pcache;
  
  if ((flags & MMO_FLG_BACKENDED) &&
      (flags & (MMO_FLG_SPIRIT | MMO_FLG_STICKY))) {
    return -EINVAL;
  }

  memobj->flags = flags;
  pcache = alloc_from_memcache(pcache_memcache);
  if (!pcache)
    return -ENOMEM;

  hat_initialize(&pcache->pagecache);
  list_init_head(&pcache->dirty_pages);
  pcache->num_dirty_pages = 0;
  rwsem_initialize(&pcache->rwlock);
  memobj->private = pcache;
  memobj->mops = &pcache_ops;

  return 0;
}

void pagecache_memobjs_prepare(void)
{
  ASSERT(pcache_memcache == NULL);
  pcache_memcache = create_memcache("Pagecache", sizeof(struct pcache),
                                    1, SMCF_PGEN | SMCF_GENERIC);
  if (!pcache_memcache) {
    panic("Can not create memory cache for pagecache objects. ENOMEM\n");
  }
}
