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
#include <mm/mman.h>
#include <mm/pfi.h>
#include <ipc/port.h>
#include <mlibc/kprintf.h>
#include <mlibc/types.h>
#include <eza/spinlock.h>
#include <eza/usercopy.h>
#include <eza/arch/mm.h>

static idx_allocator_t memobjs_ida;
static memcache_t *memobjs_memcache = NULL;
static ttree_t memobjs_tree;
static RW_SPINLOCK_DEFINE(memobjs_lock);

static int __memobjs_cmp_func(void *k1, void *k2)
{
  return (*(long *)k1 - *(long *)k2);
}

static inline memobj_t *__find_by_id_core(memobj_id_t id)
{
  return ttree_lookup(&memobjs_tree, &id, NULL);
}

static void __init_kernel_memobjs(void)
{
  int ret;

  ret = memobj_create(MMO_NTR_GENERIC, 0, USPACE_VA_TOP >> PAGE_WIDTH, NULL);  
  if (ret)
    panic("Can't create generic memory object: [ERROR %d]", ret);
}

void memobj_subsystem_initialize(void)
{
  memobj_id_t i;
  
  kprintf("[MM] Initializing memory objects subsystem...\n");
  idx_allocator_init(&memobjs_ida, CONFIG_MEMOBJS_MAX);
  for (i = 0; i < NUM_RSRV_MEMOBJ_IDS; i++)
    idx_reserve(&memobjs_ida, i);

  memobjs_memcache = create_memcache("Mmemory objects cache", sizeof(memobj_t),
                                     DEFAULT_SLAB_PAGES, SMCF_PGEN | SMCF_GENERIC);
  if (!memobjs_memcache)
    panic("memobj_subsystem_initialize: Can't create memory cache for memory objects. ENOMEM.");

  ttree_init(&memobjs_tree, __memobjs_cmp_func, memobj_t, id);
  __init_kernel_memobjs();
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

  kprintf("==> %p\n", memobj);
  memset(memobj, 0, sizeof(*memobj));
  kprintf("==> %p\n", memobj);
  if (likely(!memobj_kernel_nature(nature))) {
    spinlock_lock_write(&memobjs_lock);
    memobj->id = idx_allocate(&memobjs_ida);
    kprintf("IDX allocator returned id = %d\n", memobj->id);
    if (likely(memobj->id != IDX_INVAL))
      ASSERT(!ttree_insert(&memobjs_tree, &memobj->id));
    else {
      spinlock_unlock_write(&memobjs_lock);      
      ret = -ENOSPC;
      goto error;
    }
    
    spinlock_unlock_write(&memobjs_lock);
  }
  else {
    memobj->id = memobj_kernel_nature2id(nature);
    spinlock_lock_write(&memobjs_lock);
    ASSERT(!ttree_insert(&memobjs_tree, &memobj->id));
    spinlock_unlock_write(&memobjs_lock);
  }
    
  memobj->size = size;
  switch (nature) {
      case MMO_NTR_GENERIC:
        kprintf("zzz\n");
        kprintf("one-> %d\n", memobj->id);
        ret = generic_memobj_initialize(memobj, flags);
        kprintf("two\n");
        break;
      case MMO_NTR_PAGECACHE:
        kprintf("1) ID = %d", memobj->id);
        ret = pagecache_memobj_initialize(memobj, flags);
        kprintf("2) ID = %d", memobj->id);
        break;
      default:
        ret = -EINVAL;
        goto error;
  }
  if (out_memobj)
    *out_memobj = memobj;
  
  return ret;
  
  error:
  if (memobj) {
    spinlock_lock_write(&memobjs_lock);
    ttree_delete(&memobjs_tree, &memobj->id);
    if (likely(!memobj_kernel_nature(nature)))
      idx_free(&memobjs_ida, memobj->id);

    spinlock_unlock_write(&memobjs_lock);
    memfree(memobj);
  }

  return ret;
}

memobj_t *memobj_find_by_id(memobj_id_t memobj_id)
{
  memobj_t *ret;

  spinlock_lock_read(&memobjs_lock);
  ret = __find_by_id_core(memobj_id);
  spinlock_unlock_read(&memobjs_lock);

  return ret;
}

memobj_t *memobj_pin_by_id(memobj_id_t memobj_id)
{
  memobj_t *ret;

  spinlock_lock_read(&memobjs_lock);
  ret = __find_by_id_core(memobj_id);
  if (ret)
    pin_memobj(ret);
  
  spinlock_unlock_read(&memobjs_lock);
  return ret;
}

bool __try_destroy_memobj(memobj_t *memobj)
{
  bool ret = true;
  
  spinlock_lock_write(&memobjs_lock);
  if (unlikely(atomic_get(&memobj->users_count))) {
    spinlock_unlock_write(&memobjs_lock);
    ret = false;
    goto out;
  }

  ASSERT(ttree_delete(&memobjs_tree, &memobj->id) != NULL);
  idx_free(&memobjs_ida, memobj->id);
  spinlock_unlock_write(&memobjs_lock);

  memobj->mops->cleanup(memobj);
  memfree(memobj);
  
  out:
  return ret;
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

int sys_memobj_create(struct memobj_info *user_mmo_info)
{
  struct memobj_info mmo_info;
  int ret = 0;
  uint32_t memobj_flags;
  memobj_t *memobj = NULL;
  
  if (copy_from_user(&mmo_info, user_mmo_info, sizeof(mmo_info)))
    return -EFAULT;

  /* Validate memory object nature */
  kprintf("NATURE: %d\n", mmo_info.nature);
  if ((mmo_info.nature > 0) && (mmo_info.nature <= MMO_NTR_SRV))
    return -EPERM;
  else if ((mmo_info.nature > MMO_NTR_PROXY) || (mmo_info.nature < 0))
    return -EINVAL;

  /* Compose memory object flags */
  memobj_flags = (1 << pow2(mmo_info.lifetype)) & MMO_LIFE_MASK;
  memobj_flags |= (mmo_info.flags << MMO_FLAGS_SHIFT) & MMO_FLAGS_MASK;
  if (!(memobj_flags & MMO_LIFE_MASK) || (mmo_info.size & PAGE_MASK) || !mmo_info.size)
    return -EINVAL;

  ret = memobj_create(mmo_info.nature, memobj_flags, mmo_info.size >> PAGE_WIDTH, &memobj);
  if (ret)
    return ret;

  mmo_info.flags = (memobj->flags & MMO_FLAGS_MASK) >> MMO_FLAGS_SHIFT;
  kprintf("New memobj id = %d\n", memobj->id);
  mmo_info.id = memobj->id;
  if (copy_to_user(user_mmo_info, &mmo_info, sizeof(mmo_info))) {
    __try_destroy_memobj(memobj);
    return -EFAULT;
  }
  
  return 0;
}
