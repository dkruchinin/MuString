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
#include <mm/ptable.h>
#include <eza/spinlock.h>
#include <mlibc/stddef.h>
#include <mlibc/types.h>

struct pcache_private {
  hat_t pagecache;
  list_head_t dirty_pages;
  rw_spinlock_t cache_lock;
  uint32_t num_dirty_pages;
};

static inline void __add_dirty_page(struct pcache_private *priv, page_frame_t *page)
{
  list_add2tail(&priv->dirty_pages, &page->node);
  priv->num_dirty_pages++;
}

static inline page_frame_t *__pcache_get_page(struct pcache_private *priv, pgoff_t offset)
{
  return hat_lookup(&priv->pagecache, offset);
}

static int pcache_handle_page_fault(vmrange_t *vmr, uintptr_t addr, uint32_t pfmask)
{
  int ret = 0;
  vmm_t *vmm = vmr->parent_vmm;
  memobj_t *memobj = vmr->memobj;
  struct pcache_private *priv = memobj->private;
  pgoff_t offset = addr2pgoff(vmr, addr);
  kmap_flags_t mmap_flags = vmr->flags & KMAP_FLAGS_MASK;
  page_frame_t *page = NULL;
  
  if (unlikely(offset >= memobj->size))
    return -ENXIO;

  ASSERT(!(vmr->flags & VMR_NONE));
  ASSERT(!((memobj->flags & MMO_FLG_NOSHARED) && (vmr->flags & VMR_SHARED)));
  if (pfmask & PFLT_NOT_PRESENT) {    
    /* At first try to find out if the page is already in the cache... */
    spinlock_lock_read(&priv->cache_lock);
    page = __pcache_get_page(priv, offset);
    if (page) {
      /* Page shouldn't be fried while we're working with it. */
      pin_page_frame(page);
    }
    
    spinlock_unlock_read(&priv->cache_lock);
    if (!page) {
      /*
       * If page wasn't found in a cache, we have to prepare it
       * depending on the nature of its memory object and VM range.
       */
      if (!(memobj->flags & MMO_FLG_BACKENDED))
        ret = memobj_prepare_page_raw(memobj, &page);
      else
        ret = memobj_prepare_page_backended(memobj, &page);      
      if (ret)
        return ret;

      page->offset = offset;
      atomic_set(&page->refcount, 1);      
      if (unlikely(vmr->flags & VMR_PRIVATE)) {
        /*
         * Ok, it was a private mapping, so we don't need to put
         * the page in a cache.
         */
        goto map_page;
      }

      pin_page_frame(page);
      spinlock_lock_write(&priv->cache_lock);
      /* put page in the cache */
      ret = hat_insert(&priv->pagecache, offset, page);
      if (!ret && (memobj->flags & MMO_FLG_DPC)) {
        /*
         * If dirty pages collection facility was enabled in a given
         * memory object and writing caused the fault, put the page in
         * a dirty pages list.
         */
        if (pfmask & PFLT_WRITE) {
          atomic_inc(&page->dirtycount);
          __add_dirty_page(priv, page);
        }
        else
          mmap_flags &= ~VMR_WRITE;
      }
      else if (unlikely(ret == -EEXIST)) {
        /*
         * While we were allocating and initializing the page, somebody
         * could insert another page by given offset.
         * If so, the proper page may be easily taken from the cache by its offset
         */
          
        free_page(page);
        page = __pcache_get_page(priv, offset);
        ASSERT(page != NULL);
        spinlock_unlock_write(&priv->cache_lock);
        return 0;
      }

      spinlock_unlock_write(&priv->cache_lock);
      goto map_page;
    }
    if (unlikely(vmr->flags & VMR_PRIVATE)) {
      page_frame_t *new_page;

      /* Just copy the page if the fault occured in private mapping */
      ret = memobj_prepare_page_raw(memobj, &new_page);
      if (ret)
        return ret;

      copy_page_frame(new_page, page);
      page = new_page;
      page->offset = offset;
      atomic_set(&new_page->refcount, 1);
      unpin_page_frame(page);
    }
  }
  else if (pfmask & PFLT_WRITE) {
    if (memobj->flags & MMO_FLG_DPC) {
      if (unlikely(!atomic_inc_and_test(&page->dirtycount))) {
        spinlock_lock_write(&priv->lock);
        if (likely(page->owner == memobj))
          __add_dirty_page(memobj, page);
        spinlock_unlock_write(&priv->lock);
      }
    }

    mmap_flags &= ~VMR_WRITE;
  }

  map_page:
  pagetable_lock(&vmm->rpd);
  if (page) {
    ret = mmap_core(&vmm->rpd, addr, pframe_number(page), mmap_flags, false);
    if (ret)
      unpin_page_frame(page);
  }
  else {
    page_idx_t idx = ptable_ops.vaddr2page_idx(&vmm->rpd, addr, NULL);
    ret = mmap_core(&vmm->rpd, addr, pframe_number(page), mmap_flags, true);
  }

  pagetable_unlock(&vmm->rpd);
  return ret;
}

static int pcache_populate_pages(vmrange_t *vmr, page_idx_t npages, pgoff_t offs_pages)
{
  return -ENOTSUP;
}

static int pcache_put_page(memobj_t *memobj, pgoff_t offset, page_frame_t *page)
{
  return -ENOTSUP;
}

static page_frame_t pcache_get_page(memobj_t *memobj, pgoff_t offset)
{
  return NULL;
}

static memobj_ops_t pcacge_memobj_ops {
  .handle_page_fault = pcache_handle_page_fault;
  .populate_pages = pcache_populate_pages;
  .put_page = pcache_put_page;
  .get_page = pcache_get_page;
};

/* TODO DK: BTW how am I going to collect other(RO) pages? */
int pcache_memobj_initialize(memobj_t *memobj, uint32_t flags)
{
  struct pcache_private *priv = NULL;
  int ret = 0;
  
  if (!(flags & MMO_LIVE_MASK) || !is_powerof2(flags & MMO_LIVE_MASK))
    return -EINVAL;

  memobj->flags = flags;
  memobj->mops = &pcache_memobj_ops;
  priv = memalloc(sizeof(*priv));
  if (!priv)
    return -ENOMEM;
  if (flags & MMO_FLG_BACKENDED) {
    memobj->backend = memobj_create_backend();
    if (!memobj->backend) {
      ret = -ENOMEM;
      goto error;
    }
  }
  if (flags & (MMO_FLG_SPIRIT | MMO_FLG_IMMORTAL))
    atomic_set(&memobj->users_count, 1);

  hat_initialize(&priv->pagecache);
  list_init_head(&priv->dirty_pages);
  rw_spinlock_initialize(&priv->cache_lock);
  priv->num_dirty_pages = 0;
  memobj->private = priv;

  /* TODO DK: add backend support */
  return ret;
  
  error:
  if (priv)
    memfree(priv);
  if (memobj->backend)
    memobj_release_backend(memobj->backend);

  return ret;
}
