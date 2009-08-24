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
 * mstring/sync/umutex.c: Implementation of userspace mutex core functionality.
 */

#include <arch/types.h>
#include <sync/mutex.h>
#include <mstring/sync.h>
#include <mstring/task.h>
#include <mstring/usercopy.h>
#include <mstring/time.h>
#include <mstring/timer.h>
#include <mm/slab.h>
#include <arch/atomic.h>
#include <sync/mutex.h>
#include <mstring/signal.h>

#define __LOCK_EVENT(e) spinlock_lock(&(e)->__lock)
#define __UNLOCK_EVENT(e) spinlock_unlock(&(e)->__lock)

static sync_uevent_t *__allocate_uevent(void);

static int __rawevent_control(kern_sync_object_t *obj,ulong_t cmd,ulong_t arg)
{
  sync_uevent_t *e=__UEVENT_OBJ(obj);
  wqueue_task_t wt;

  switch( cmd ) {
      case __SYNC_CMD_EVENT_WAIT:
      case __SYNC_CMD_EVENT_TIMEDWAIT:
      {
        timeval_t tspec;
        ktimer_t ktimer;
        ulong_t tm;

        if (cmd == __SYNC_CMD_EVENT_TIMEDWAIT) {
          if (copy_from_user(&tspec, (void *)arg, sizeof(tspec))) {
            return -EFAULT;
          }
          if (!timeval_is_valid(&tspec)) {
            return -EINVAL;
          }

          __LOCK_EVENT(e);
          tm = time_to_ticks(&tspec) + system_ticks;
          ktimer.da.d.target = current_task();
          init_timer(&ktimer, tm, DEF_ACTION_UNBLOCK);
          add_timer(&ktimer);
        }
        else {
          __LOCK_EVENT(e);
        }

        while( !e->__ecount) {
          /* Event will be checked one more time later. */

          waitqueue_prepare_task(&wt,current_task());
          wt.private=e;
          waitqueue_insert_core(&e->__wq,&wt, WQ_INSERT_SLEEP_INR);
          __UNLOCK_EVENT(e);

          if( task_was_interrupted(current_task()) ) {
            waitqueue_delete(&wt,WQ_DELETE_SIMPLE);
            if (cmd == __SYNC_CMD_EVENT_TIMEDWAIT) {
              delete_timer(&ktimer);
            }

            return -EINTR;
          }
          __LOCK_EVENT(e);
          if ((cmd == __SYNC_CMD_EVENT_TIMEDWAIT) &&
              (tm <= system_ticks)) {
            e->__ecount--;
            __UNLOCK_EVENT(e);
            delete_timer(&ktimer);
            return -ETIMEDOUT;
          }
        }

        e->__ecount--;
        __UNLOCK_EVENT(e);
        if (cmd == __SYNC_CMD_EVENT_TIMEDWAIT) {
          delete_timer(&ktimer);
        }

        break;
      }
      case  __SYNC_CMD_EVENT_SIGNAL:
        __LOCK_EVENT(e);
        e->__ecount++;
        __UNLOCK_EVENT(e);
        waitqueue_pop(&e->__wq,NULL);
        break;
      case __SYNC_CMD_EVENT_BROADCAST:
        __LOCK_EVENT(e);
        while (!waitqueue_is_empty(&e->__wq)) {
          wqueue_task_t *waiter = waitqueue_first_task(&e->__wq);

          e->__ecount++;
          waitqueue_delete_core(waiter, WQ_DELETE_WAKEUP);
        }

        __UNLOCK_EVENT(e);
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
