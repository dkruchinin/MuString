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
 * (c) Copyright 2008 Michael Tsymbalyuk <mtzaurus@gmail.com>
 *
 * eza/sync/umutex.c: Implementation of userspace mutex core functionality.
 */

#include <mlibc/types.h>
#include <eza/mutex.h>
#include <eza/sync.h>
#include <eza/task.h>
#include <eza/usercopy.h>
#include <mm/slab.h>
#include <eza/arch/atomic.h>
#include <eza/mutex.h>

static pthread_mutexattr_t __default_mutex_attrs={(PTHREAD_PROCESS_PRIVATE)};

static sync_umutex_t *__allocate_umutex(void);

/* Mutex-related logic. */
static int __mutex_control(kern_sync_object_t *obj,ulong_t cmd,ulong_t arg)
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

static struct __kern_sync_object *__umutex_clone(struct __kern_sync_object *obj)
{
  sync_umutex_t *new=__allocate_umutex();
  sync_umutex_t *source=(sync_umutex_t *)obj;

  if( new ) {
    new->k_syncobj.id=source->k_syncobj.id;
    return &new->k_syncobj;
  }
  return NULL;
}

static sync_obj_ops_t __mutex_ops = {
  .control=__mutex_control,
  .dtor=sync_default_dtor,
  .clone=__umutex_clone,
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

int sync_create_mutex(kern_sync_object_t **obj,void *uobj,
                           uint8_t *attrs,ulong_t flags)
{
  sync_umutex_t *umutex=__allocate_umutex();
  pthread_mutex_t *pum;

  if( !umutex ) {
    return -ENOMEM;
  }  

  if( flags ) {
    /* Create a locked mutex ? */
    mutex_lock(&umutex->__kmutex);
  }

  /* Setup mutex attributes. */
  pum=(pthread_mutex_t *)uobj;
  if( !attrs ) {
    attrs=(uint8_t*)&__default_mutex_attrs;
    /* Process attributes here. */
  }
  if( copy_to_user(&pum->__attrs,attrs,sizeof(pthread_mutexattr_t)) ) {
    goto out;
  }

  *obj=(kern_sync_object_t*)umutex;
  return 0;
out:
  if( flags ) {
    mutex_unlock(&umutex->__kmutex);
  }
  memfree(umutex);
  return -EINVAL;
}
