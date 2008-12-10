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
 * (c) Copyright 2006,2007,2008 MString Core Team <http://mstring.berlios.de>
 * (c) Copyright 2008 Michael Tsymbalyuk <mtzaurus@gmail.com>
 *
 * eza/sync/umutex.c: Implementation of userspace mutex core functionality.
 */

#include <eza/arch/types.h>
#include <eza/mutex.h>
#include <eza/sync.h>
#include <kernel/vm.h>
#include <eza/task.h>
#include <mm/slab.h>
#include <eza/arch/atomic.h>
#include <eza/mutex.h>

/* Mutex-related logic. */
static status_t __mutex_control(void *obj,ulong_t cmd,ulong_t arg)
{
  sync_umutex_t *umutex=__UMUTEX_OBJ(obj);

  switch(cmd) {
    case __SYNC_CMD_MUTEX_LOCK:
      mutex_lock(&umutex->__kmutex);
      return 0;
    case __SYNC_CMD_MUTEX_UNLOCK:
      mutex_unlock(&umutex->__kmutex);
      return 0;
    case __SYNC_CMD_MUTEX_TRYLOCK:
      if( mutex_trylock(&umutex->__kmutex) ) {
        return 0;
      } else {
        return -EBUSY;
      }
    default:
      return -EINVAL;
  }
}

static void __mutex_dtor(void *obj)
{
  memfree(obj);
}

static sync_obj_ops_t __mutex_ops = {
  .control=__mutex_control,
  .dtor=__mutex_dtor,
};

static sync_umutex_t *__allocate_umutex(void)
{
  sync_umutex_t *umutex=memalloc(sizeof(*umutex));

  if( umutex ) {
    memset(umutex,0,sizeof(*umutex));
    umutex->k_syncobj.type=__SO_MUTEX;
    umutex->k_syncobj.ops=&__mutex_ops;
    atomic_set(&umutex->k_syncobj.refcount,1);
    mutex_initialize(&umutex->__kmutex);
  }

  return umutex;
}

status_t sync_create_mutex(kern_sync_object_t **obj,ulong_t flags)
{
  sync_umutex_t *umutex=__allocate_umutex();

  if( !umutex ) {
    return -ENOMEM;
  }  

  *obj=(kern_sync_object_t*)umutex;
  return 0;
}
