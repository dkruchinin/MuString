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
 * mm/memobj_pagecacge.c - Pagecached memory object implementation
 *
 */

#include <config.h>
#include <ds/list.h>
#include <ds/hat.h>
#include <mm/vmm.h>
#include <mm/memobj.h>
#include <mm/slab.h>
#include <mm/pfalloc.h>
#include <mm/rmap.h>
#include <eza/spinlock.h>
#include <mlibc/stddef.h>
#include <mlibc/types.h>

#ifdef CONFIG_DEBUG_PAGECACHE
#define PAGECACHE_DBG(fmt, args...)                          \
  do {                                                       \
    kprintf("[PAGECACHE_DBG] (%s) ", __FUNCTION__);          \
    kprintf(fmt, ##args);                                    \
  } while (0)
#else
#define PAGECACHE_DBG(fmt, args...)
#endif /* CONFIG_DEBUG_PAGECACHE */


static inline void __add_dirty_page(memobj_t *memobj, page_frame_t *page)
{
  struct pcache_private *priv = memobj->priv;
  
  if (!(memobj->flags & MMO_FLG_DPC)) {
    /* TODO DK: simply mark page as dirty and put it in LRU or something... */
    return;
  }
  
  if (atomic_test_and_set_bit(&page->flags, pow2(PF_DIRTY)))
    return; /* Page is already marked as dirty. */
  
  PAGECACHE_DBG("(pid %ld): New dirty page %#x by offset %#x\n",
                current_task()->pid, pframe_number(page), page->offset);
  list_add2tail(&priv->dirty_pages, &page->node);
  priv->num_dirty_pages++;
}


static page_frame_t *copy_private_page(page_frame_t *src)
{
  page_frame_t *copy = alloc_page(AF_USER);
  
  if (!copy)
    return NULL;

  copy_page_frame(copy, src);
  copy->offset = src->offset;
  copy->owner = src->owner;
  atomic_set(&copy->refcount, 1);
  atomic_set(&copy->dirtycount, 0);
  unpin_page_frame(src);
  
  return copy;
}


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

static int handle_not_present_fault(vmrange_t *vmr, pgoff_t offset, uint32_t pfmask, page_frame_t **page)
{
  page_frame_t *p;
  memobj_t *memobj = vmr->memobj;
  vmm_t *vmm = vmr->parent_vmm;
  struct pcache_private *priv = memobj->priv;
  int ret = 0;
  
  if (!(memobj->flags & MMO_FLG_BACKENDED)) {
    ret = memobj_prepare_page_raw(memobj, &p);
    if (ret)
      goto out;

    p->offset = offset;
    //p->owner = memobj; XXX
    pin_page_frame(p);
    if ((vmr->flags & VMR_PRIVATE) && (pfmask & PFLT_WRITE)) {      
      *page = p;
      goto out;
    }

    /*
     * If memory doesn't have backend, all pages in #PF handler are
     * allocated explicitly. Thus, after page is allocated it has to be
     * inserted in a cache.
     */
    spinlock_lock_write(&priv);
    ret = hat_insert(&priv->pagecahce, offset, p);
    if (!ret && (memobj->flags & MMO_FLG_DPC) && (pfmask & PFLT_WRITE)) {
      /*
       * If dirty pages collection facility was enabled in a given
       * memory object and writing caused the fault, put the page in
       * a dirty pages list.
       */
        __add_dirty_page(priv, page);
    }
    else if (unlikely(ret == -EEXIST)) {
      /*
       * While we were allocating and initializing the page, somebody
       * could insert another page by the same offset.
       * If so, the proper page may be easily taken from the cache by its offset
       */
      unpin_page_frame(p);
      p = hat_lookup(&priv->pagecache, offset);
      ASSERT(p != NULL);
      pin_page_frame(p);
      *page = p;
      ret = 0;
    }
    
    spinlock_unlock_write(&priv);
  }
  else {
    /*
     * Backended memory objects requires their backends to allocate (and may be
     * fill with some content) them a page. After backend replied that it did its work,
     * required page *must be* already in cache, thus here we don't need to explicitly insert
     * it in.
     */
    ret = prepare_backended_page(memobj, offset, &p);
    if (ret)
      goto out;

    *page = p;
    if (pfmask & PFLT_WRITE) {
      if (vmr->flags & VMR_PRIVATE) {
        p = copy_private_page(p);
        if (!p) {
          unpin_page_frame(p);
          ret = -ENOMEM;
        }
        
        goto out;
      }
      if (memobj->flags & MMO_FLG_DPC) {
        spinlock_lock_write(&priv->cache_lock);
        __add_dirty_page(priv, p);
        spinlock_unlock_write(&priv->cache_lock);
      }
    }
  }

out:
  return ret;
}

static int pcache_handle_page_fault(vmrange_t *vmr, uintptr_t addr, uint32_t pfmask)
{
  int ret = 0;
  vmm_t *vmm = vmr->parent_vmm;
  memobj_t *memobj = vmr->memobj;
  struct pcache_private *priv = memobj->private;
  pgoff_t offset = addr2pgoff(vmr, addr);
  page_frame_t *page = NULL;
  vmrange_flags_t mmap_flags = vmr->flags;
 
  if (unlikely(offset >= memobj->size)) {
    PAGECACHE_DBG("(pid %ld) handlig #PF on %p by invalid offset %#x (max = %#x)!\n",
                  vmm->owner->pid, addr, offset, memobj->size);
    return -ENXIO;
  }
  if (unlikely((memobj->flags & MMO_FLG_BACKENDED) && (memobj->backend == NULL))) {
    PAGECACHE_DBG("(pid %ld) handling #PF on %p: memory object #%d has MMO_FLG_BACKENDED "
                  "but has not backend itself!\n", vmm->owner->pid, addr, memobj->id);
    return -EINVAL;
  }

  ASSERT_DBG(!(vmr->flags & VMR_NONE));
  ASSERT_DBG(!(vmr->flags & VMR_PHYS));
  ASSERT_DBG(!((memobj->flags & MMO_FLG_NOSHARED) && (vmr->flags & VMR_SHARED)));

  if (pfmask & PFLT_NOT_PRESENT) {
    /*
     * With "page cache" memory object there might be exactly two possible situations
     * when page is not present:
     *  1) It is already in a cache(HAT). In this case we simply mmap it to address
     *     space of faulted process(of course after all flags are checked)
     *  2) It is not in a cache. In this case new page has to be allocated(implicitly inkernel
     *     or explicitly by backend. It depends on the nature of given object) and inserted in
     *     cache by given offset.
     */    
    spinlock_lock_read(&priv->cache_lock);
    page = hat_lookup(&priv->pagecache, offset);
    if (page) {
      PAGECACHE_DBG("(pid %ld) #PF: page by offset %ld[idx = %#x] is already in a cache.\n",
                    vmm->owner->pid, offset, pframe_number(page));
      /* We don't want page to be fried while we're working with it. */
      pin_page_frame(page);
      if (pfmask & PFLT_WRITE) {
        __add_dirty_page(memobj, page);
        mmap_flags &= ~VMR_WRITE;
      }
    }

    spinlock_unlock_read(&priv->cache_lock);
    
    if (!page) {
      if (!(memobj->flags & MMO_FLG_BACKENDED)) {
        ret = memobj_prepare_page_raw(memobj, &p);
        if (ret) {
          PAGECACHE_DBG("(pid %ld) Failed to prepare page for non-backended memory object "
                        "by address %p. [RET = %d]\n", vmm->owner->pid, addr, ret);
          return ret;
        }
        if (unlikely(vmr->flags & VMR_PRIVATE) && (pfmask & VMR_WRITE)) {
        }
      }
      else {
        /* ask backend to allocate us a page */
        ret = prepare_backended_page(memobj, offset, &p);
        if (ret) {
          PAGECACHE_DBG("(pid %ld) Failed to prepare page for backended memory object "
                        "by address %p. [RET = %d]\n", vmm->owner->pid, addr, ret);
          return ret;
        }
      }
      
      
      ret = handle_not_present_fault(vmr, offset, pfmask, &page);
      if (ret) {
        PAGECACHE_DBG("(pid %ld) Failed to handle \"not-present\" fault by offset %#x. [RET = %d]\n",
                      vmm->owner->pid, offset, ret);
        return ret;
      }
    }    
    pagetable_lock(&vmm->rpd);
    ret = mmap_one_page(&vmm->rpd, addr, pframe_number(page), mmap_flags);
    pagetable_unlock(&vmm->rpd);
    if (ret)
      unpin_page_frame(page);
  }
  else {
    page_frame_t *cpage;

    ASSERT_DBG(!(vmr->flags & VMR_PRIVATE)); /* FIXME DK: Implement write-faults with VMR_PRIVATE mappings */
    ASSERT_DBG(pfmask & PFLT_WRITE);
    pagetable_lock(&vmm->rpd);
    idx = vaddr2page_idx(&vmm->rpd, addr);
    pagetable_unlock(&vmm->rpd);

    /*
     * Very unpleasent situation may occur when an owner of the memory object
     * removed a page from the cachethat already has been mapped to one or more
     * address spaces. Moreover, beside this terrible thing it explicitly or implicitly
     * could hapily insert *another* page into the cache by the same offset.
     * Here we have to take care about such kind of crap: if the page task faults on, was only removed
     * from the cache, it have to be synced(if necessary) and inserted back into the cache,
     * otherwise(if it was replaced), old page have to be freed and the new one(taken from the cache)
     * should become a replacement the old one.
     */
    page = pframe_by_number(idx);
    ASSERT(page->owner == memobj);
    spinlock_lock_read(&priv->cache_lock);
    cpage = __pcache_get_page(priv, page->offset);        
    if (unlikely(cpage != page)) {
      unpin_page_frame(page);
      page = cpage;
    }
    
    spinlock_unlock_read(&priv->cache_lock);
    if (unlikely(!page)) {
      ASSERT(true); /* TODO DK: sync page with backend and insert it into the cache */      
    }
    if (memobj->flags & MMO_FLG_DPC) {
      if (unlikely(!atomic_get(&page->dirtycount))) {
        spinlock_lock_write(&priv->cache_lock);
        if (likely(!atomic_get(&page->dirtycount)))
          __add_dirty_page(priv, page);
        
        spinlock_unlock_write(&priv->cache_lock);
      }
    }

    mmap_flags |= VMR_WRITE;
  }

map_page:
  pagetable_lock(&vmm->rpd);
  if (pfmask & PFLT_NOT_PRESENT) {
    idx = vaddr2page_idx(&vmm->rpd, addr);
    if (unlikely(idx != PAGE_IDX_INVAL)) {
      ret = 0;
      unpin_page_frame(page);
      goto out;
    }
  }

  ret = mmap_one_page(&vmm->rpd, addr, pframe_number(page), mmap_flags);
  pagetable_unlock(&vmm->rpd);
  
out:
  return ret;
}

static int pcache_populate_pages(vmrange_t *vmr, uintptr_t addr, page_idx_t npages)
{
  return -ENOTSUP;
}

static int pcache_put_page(memobj_t *memobj, pgoff_t offset, page_frame_t *page)
{
  return -ENOTSUP;
}

static int pcache_get_page(memobj_t *memobj, pgoff_t offset, page_frame_t **page)
{
  return -ENOTSUP;
}

static void pcache_cleanup(memobj_t *memobj)
{
  return; /* Not supported yet... */
}

static memobj_ops_t pcache_memobj_ops = {
  .handle_page_fault = pcache_handle_page_fault,
  .populate_pages = pcache_populate_pages,
  .put_page = pcache_put_page,
  .get_page = pcache_get_page,
  .cleanup = pcache_cleanup,
};

/* TODO DK: BTW how am I going to collect other(RO) pages? */
int pagecache_memobj_initialize(memobj_t *memobj, uint32_t flags)
{
  struct pcache_private *priv = NULL;
  int ret = 0;
  
  if (!(flags & MMO_LIFE_MASK) || !is_powerof2(flags & MMO_LIFE_MASK))
    return -EINVAL;

  memobj->flags = flags;
  memobj->mops = &pcache_memobj_ops;
  priv = memalloc(sizeof(*priv));
  if (!priv)
    return -ENOMEM;
  
  atomic_set(&memobj->users_count, 1);
  hat_initialize(&priv->pagecache);
  list_init_head(&priv->dirty_pages);
  rw_spinlock_initialize(&priv->cache_lock);
  priv->num_dirty_pages = 0;
  memobj->private = priv;

  return ret;  
}
