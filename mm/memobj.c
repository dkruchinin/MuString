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
 * mm/memobj.c - Memory objects subsystem
 *
 */

#include <config.h>
#include <ds/list.h>
#include <ds/idx_allocator.h>
#include <mm/page.h>
#include <mm/pfalloc.h>
#include <mm/slab.h>
#include <mm/memobj.h>
#include <mm/pfi.h>
#include <ipc/port.h>
#include <mlibc/kprintf.h>
#include <mlibc/types.h>
#include <eza/spinlock.h>
#include <eza/arch/mm.h>

static idx_allocator_t memobjs_ida;
static memcache_t *memobjs_memcache = NULL;
static ttree_t memobjs_tree;
static SPINLOCK_DEFINE(memobjs_tree_lock);

static int __memobjs_cmp_func(void *k1, void *k2)
{
  return (*(long *)k1 - *(long *)k2);
}

void memobj_subsystem_initialize(void)
{
  memobj_id_t i;
  
  kprintf("[MM] Initializing memory objects subsystem...\n");
  idx_allocator_init(&memobjs_ida, CONFIG_MEMOBJS_MAX);
  for (i = 0; i < NUM_RSRV_MEMOBJ_IDS)
    idx_reserve(&memobjs_ida, i);

  memobjs_memcache = create_memcache("Mmemory objects cache", sizeof(memobj_t),
                                     DEFAULT_SLAB_PAGES, SMCF_PGEN | SMCF_GENERIC);
  if (!memobjs_memcache)
    panic("memobj_subsystem_initialize: Can't create memory cache for memory objects. ENOMEM.");

  ttree_init(&memobjs_tree, __memobjs_cmp_func, memobj_t, id);
}

int memobj_create(memobj_nature_t nature, uint32_t flags, pgoff_t size, /* OUT */ memobj_t **out_memobj)
{
  memobj_t *memobj = NULL;
  int ret = 0;

  memobj = alloc_from_memcache(memobjs_memcache);
  if (!memobj) {
    ret = -ENOMEM;
    goto error;
  }
  if (likely(!memobj_kernel_nature(nature))) {
    spinlock_lock(&memobjs_lock);
    memobj->id = idx_allocate(&memobjs_ida);    
    spinlock_unlock(&memobjs_lock);
  }
  else
    memobj->id = memobj_kernel_nature2id(nature);  
  if (likely(memboj->id != IDX_INVAL))
    ASSERT(!ttree_insert(&memobjs_tree, &memobj->id));
  else {
    ret = -ENOSPC;
    goto error;
  }
  
  memset(memobj, 0, sizeof(*memobj));
  memobj->size = size;
  switch (nature) {
      case MMO_NTR_GENERIC:
        ret = generic_memobj_intialize(memobj, flags);
        break;
      case MMO_NTR_PAGECACHE:
        ret = pagecache_memobj_initialize(memobj, flags);
        break;
      default:
        ret = -EINVAL;
        goto error;
  }

  *out_memobj = memobj;
  return ret;
  
  error:
  if (memobj)
    memfree(memobj);

  return ret;
}

memobj_t *memobj_find_by_id(memobj_id_t memobj_id)
{
  if (memobj_id == GENERIC_MEMOBJ_OD)
    return &generic_memobj;
  else {
    memobj_t *ret;

    spinlock_lock(&memobjs_lock);
    ret = ttree_lookup(&memobjs_tree, &memobj_id, NULL);
    spinlock_unlock(&memobjs_lock);

    return ret;
  }
}

memobj_backend_t *memobj_create_backend(void)
{
  memobj_backend_t *backend = memalloc(sizeof(*backend));

  if (backend) {
    backend->server = NULL;
    backend->port_id = IPC_PORT_ID_INVAL;
  }

  return backend;
}

void memobj_release_backend(memobj_backend_t *backend)
{
  /*
   * FIXME DK: may be it has a sence to report the server
   * abount memobject releasing?
   */
  memfree(backend);
}

int memobj_prepare_page_raw(memobj_t *memobj, page_frame_t **page)
{
  int ret = 0;
  page_frame_t *pf = NULL;

  pf = alloc_page(AF_USER | AF_ZERO);
  if (pf) {
    pf->owner = memobj;
    atomic_set(&pf->dirtycount, 0);
    atomic_set(&pf->refcount, 0);
  }
  else
    ret = -ENOMEM;

  *page = pf;
  return ret;
}

int memobj_prepare_page_backended(memobj_t *memobj, page_frame_t **page)
{
  *page = NULL;
  return -ENOTSUP;
}
