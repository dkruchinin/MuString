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

#include <eza/arch/types.h>
#include <eza/mutex.h>
#include <eza/sync.h>
#include <eza/task.h>
#include <eza/usercopy.h>
#include <mm/slab.h>
#include <eza/arch/atomic.h>
#include <eza/mutex.h>
#include <eza/signal.h>

#define __LOCK_EVENT(e) spinlock_lock(&(e)->__lock)
#define __UNLOCK_EVENT(e) spinlock_unlock(&(e)->__lock)

static sync_uevent_t *__allocate_uevent(void);

static int __rawevent_control(kern_sync_object_t *obj,ulong_t cmd,ulong_t arg)
{
  sync_uevent_t *e=__UEVENT_OBJ(obj);
  wqueue_task_t wt;

  switch( cmd ) {
    case __SYNC_CMD_EVENT_WAIT:
      __LOCK_EVENT(e);
      while( !e->__ecount) {
        /* Event will be checked one more time later. */
        __UNLOCK_EVENT(e);

        waitqueue_prepare_task(&wt,current_task());
        wt.private=e;
        waitqueue_push_intr(&e->__wq,&wt);

        if( task_was_interrupted(current_task()) ) {
          waitqueue_delete(&wt,WQ_DELETE_SIMPLE);
          return -EINTR;
        }
        __LOCK_EVENT(e);
      }
      e->__ecount=0;
      __UNLOCK_EVENT(e);
      break;
    case  __SYNC_CMD_EVENT_SIGNAL:
      __LOCK_EVENT(e);
      e->__ecount++;
      __UNLOCK_EVENT(e);
      waitqueue_pop(&e->__wq,NULL);
      break;
    default:
      return -EINVAL;
  }
  return 0;
}

static struct __kern_sync_object *__urawevent_clone(struct __kern_sync_object *obj)
{
  sync_uevent_t *new=__allocate_uevent();
  sync_uevent_t *source=(sync_uevent_t *)obj;

  if( new ) {
    new->k_syncobj.id=source->k_syncobj.id;
    return &new->k_syncobj;
  }
  return NULL;
}

static sync_obj_ops_t __rawevent_ops = {
  .control=__rawevent_control,
  .dtor=sync_default_dtor,
  .clone=__urawevent_clone,
};

static sync_uevent_t *__allocate_uevent(void)
{
  sync_uevent_t *e=memalloc(sizeof(*e));

  if( e ) {
    memset(e,0,sizeof(*e));
    e->k_syncobj.type=__SO_RAWEVENT;
    e->k_syncobj.ops=&__rawevent_ops;
    atomic_set(&e->k_syncobj.refcount,1);

    waitqueue_initialize(&e->__wq);
    spinlock_initialize(&e->__lock);
  }

  return e;
}

int sync_create_uevent(kern_sync_object_t **obj,void *uobj,
                            uint8_t *attrs,ulong_t flags)
{
  sync_uevent_t *e=__allocate_uevent();

  if( !e ) {
    return -ENOMEM;
  }

  *obj=(kern_sync_object_t*)e;
  return 0;
}
