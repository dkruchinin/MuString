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
#include <mm/memobjctl.h>
#include <mm/pfi.h>
#include <ipc/ipc.h>
#include <ipc/channel.h>
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

static int reset_memobj_backend(memobj_t *memobj, struct memobj_backend_info *backend_info)
{
  int ret;
  task_t *server_task;
  ipc_gen_port_t *server_port;
  
  ASSERT_DBG(memobj->backend != NULL);
  server_task = pid_to_task(backend_info->server_pid);
  if (!server_task)
    return -ESRCH;

  server_port = ipc_get_port(server_task, backend_info->port_id);
  if (!server_port) {
    ret = -ENOENT;
    ipc_put_port(server_port);
    goto release_task;
  }
  
  ret = ipc_open_channel_raw(server_port, IPC_BLOCKED_ACCESS, &memobj->backend->channel);
  if (ret) {
    ipc_put_port(server_port);
    goto release_task;
  }

release_task:
  release_task(server_task);
  return ret;
}

static int create_memobj_backend(memobj_t *memobj, struct memobj_backend_info *backend_info)
{
  int ret = 0;
  
  ASSERT_DBG(memobj->backend == NULL);
  memobj->backend = memalloc(sizeof(*(memobj->backend)));
  if (!memobj->backend)
    return -ENOMEM;

  rw_spinlock_initialize(&memobj->backend->rwlock);
  ret = reset_memobj_backend(memobj, backend_info);
  if (ret)
    goto release_backend;

  return 0;
  
release_backend:
  memfree(memobj->backend);
  memobj->backend = NULL;
  return ret;
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

  memset(memobj, 0, sizeof(*memobj));
  spinlock_initialize(&memobj->members_lock);
  if (likely(!memobj_kernel_nature(nature))) {
    spinlock_lock_write(&memobjs_lock);
    memobj->id = idx_allocate(&memobjs_ida);
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
        ret = generic_memobj_initialize(memobj, flags);
        break;
      case MMO_NTR_PAGECACHE:
        ret = pagecache_memobj_initialize(memobj, flags);
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

int memobj_prepare_page_raw(memobj_t *memobj, page_frame_t **page)
{
  int ret = 0;
  page_frame_t *pf = NULL;

  pf = alloc_page(AF_USER | AF_ZERO);
  if (pf) {
    pf->owner = memobj;
    atomic_set(&pf->dirtycount, 0);
    atomic_set(&pf->refcount, 1);
  }
  else
    ret = -ENOMEM;

  *page = pf;
  return ret;
}

int memobj_prepare_page_backended(memobj_t *memobj, pgoff_t offset, page_frame_t **page)
{
  /* TODO DK: implement */
  /*struct memobj_rem_request req;
  uintptr_t repl_addr;
  long ret;
  
  ASSERT((memobj->flags & MMO_FLG_BACKENDED) && (memobj->backend != NULL));
  memset(&req, 0, sizeof(req));
  req.type = MREQ_TYPE_GETPAGE;
  req.pg_offset = offset;

  ret = sys_port_send(memobj->backend)*/
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
  mmo_info.id = memobj->id;
  if (copy_to_user(user_mmo_info, &mmo_info, sizeof(mmo_info))) {
    __try_destroy_memobj(memobj);
    return -EFAULT;
  }
  
  return 0;
}

int sys_memobj_control(memobj_id_t memobj_id, int cmd, long uarg)
{
  memobj_t *memobj = NULL;
  int ret = 0;

  memobj = memobj_pin_by_id(memobj_id);
  if (!memobj) {
    ret = -ENOENT;
    goto out;
  }

  switch (cmd) {
      case MEMOBJ_CTL_CHANGE_INFO:
        ret = -ENOTSUP;
        goto out;
      case MEMOBJ_CTL_GET_INFO:
      {
        struct memobj_info mmo_info;

        if (copy_from_user(&mmo_info, (void *)uarg, sizeof(mmo_info))) {
          ret = -EFAULT;
          goto out;
        }

        ret = -ENOTSUP;
        goto out;
      }
      case MEMOBJ_CTL_TRUNC:
        ret = -ENOTSUP;
        goto out;
      case MEMOBJ_CTL_PUT_PAGE:
        ret = -ENOTSUP;
        goto out;
      case MEMOBJ_CTL_SET_BACKEND:
      {
        if (memobj->id == GENERIC_MEMOBJ_ID) {
          ret = -EPERM;
          goto out;
        }
        if (!(memobj->flags & MEMOBJ_BACKENDED)) {
          ret = -EINVAL;
          goto out;
        }
        else {
          struct memobj_backend_info backend_info;

          if (copy_from_user(&backend_info, (void *)uarg, sizeof(backend_info))) {
            ret = -EFAULT;
            goto out;
          }
          if (memobj->backend) {
            spinlock_lock_write(&memobj->backend->rwlock);
            ipc_destroy_channel(memobj->backend->channel);
            memobj->backend->channel = NULL;
            ret = reset_memobj_backend(memobj, &backend_info);
            spinlock_unlock_write(&memobj->backend->rwlock);
          }
          else {
            ret = create_memobj_backend(memobj, &backend_info);
          }
          
          goto out;
        }
      }
      case MEMOBJ_CTL_GET_BACKEND:
        ret = -ENOTSUP;
        goto out;
      default:
        ret = -EINVAL;
  }

  out:
  if (memobj)
    unpin_memobj(memobj);
  
  return ret;  
}
