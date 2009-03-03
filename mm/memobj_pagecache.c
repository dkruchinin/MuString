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
  hat_t pagecahce;
  list_head_t dirty_pages;
  rw_spinlock_t cache_lock;
  uint32_t num_dirty_pages;
};

static inline page_frame_t *__pcache_get_page(struct pcache_private *priv, pgoff_t offset)
{
  return hat_lookup(&priv->pagecache, offset);
}

static int pcache_handle_page_fault(vmrange_t *vmr, uintptr_t addr, uint32_t pfmask)
{
  int ret = 0;
  vmm_t *vmm = vmr->parent_mm;
  memobj_t *memobj = vmrange->memobj;
  struct pagecache_private *priv = memobj->private;
  pgoff_t offset = addr2pgoff(vmr, addr);
  kmap_flags_t mmap_flags = vmr->flags & KMAP_FLAGS_MASK;
  
  if (unlikely(offset >= memobj->size))
    return -ENXIO;

  ASSERT(!(vmr->flags & VMR_NONE));
  if (pfmask & PFLT_NOT_PRESENT) {
    page_frame_t *page;

    spinlock_lock_read(&priv->cache_lock);
    page = __pcache_get_page(priv, offset);
    if (!page) {
      if (!(memobj->flags & MMO_FLG_BACKENDED)) {
        page = pfalloc(AF_ZERO | AF_USER);
        if (!page) {
          ret = -ENOMEM;
          spinlock_unlock_read(&priv->cache_lock);
          goto out;
        }

        atomic_set(&page->refcount, 1);
        atomic_set(&page->dirtycount, 0);
        page->offset = offset;
        hat_insert(&priv->pagecache, offset, page);
      }
      else
        /* TODO DK: implement handle case for backeneded memobj. */
    }

    spinlock_unlock_read(&priv->cache_lock);
    if (memobj->flags & MMO_FLG_DPC)
      mmap_flags &= ~VMR_WRITE;
    if (pfmask & PFLT_READ)
      goto map_page;
  }
  if (!(memobj->flags & MMO_FLG_DPC))
    goto map_page;
  if (unlikely(!atomic_inc_and_test(&page->dirtycount))) {
    spinlock_lock_write(&priv->lock);
    list_add2tail(&priv->dirty_list, &page->node);
    priv->num_dirty_pages++;
    spinlock_unlock_write(&priv->lock);
  }

  map_page:
  pagetable_lock(&vmm->rpd);
  if (likely(ptable_ops.vaddr2page_idx(&vmm->rpd, addr, NULL) != PAGE_IDX_INVAL))
    ret = mmap_core(&vmm->rpd, addr, pframe_number(page), mmap_flags);
  
  pagetable_unlock(&vmm->rpd);  
  return ret;
}

static int pcache_populate_pages(vmrange_t *vmr, page_idx_t npages, pgoff_t offs_pages)
{
  return 0;
}

static int pcache_put_page(memobj_t *memobj, pgoff_t offset, page_frame_t *page)
{
  return 0;
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
  struct pcache_private *priv;
  
  if (!(flags & MMO_LIVE_MASK) || !is_powerof2(flags & MMO_LIVE_MASK))
    return -EINVAL;

  memobj->flags = flags;
  memobj->mops = &pcache_memobj_ops;
  priv = memalloc(sizeof(*priv));
  if (!priv)
    return -ENOMEM;

  hat_initialize(&priv->pagecache);
  list_init_head(&priv->dirty_pages);
  rw_spinlock_initialize(&priv->cache_lock);
  priv->num_dirty_pages = 0;
  memobj->private = priv;

  /* TODO DK: add backend support */
  return 0;
}
