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
 * (c) Copyright 2006,2007,2008,2009 MString Core Team <http://mstring.jarios.org>
 * (c) Copyright 2009 Michael Tsymbalyuk <mtzaurus@gmail.com>
 *
 * mstring/sync/urwsem.c: Implementation of userspace R/W semaphores.
 */

#include <mstring/types.h>
#include <sync/mutex.h>
#include <mstring/sync.h>
#include <mstring/task.h>
#include <mstring/usercopy.h>
#include <mm/slab.h>
#include <arch/atomic.h>
#include <sync/rwsem.h>
#include <mstring/signal.h>

static sync_urw_semaphore_t *__allocate_urw_semaphore(void);

static int __rwsem_control(kern_sync_object_t *obj,ulong_t cmd,ulong_t arg)
{
  sync_urw_semaphore_t *urwsem=__URWSEMAPHORE_OBJ(obj);
  task_t *caller=current_task();

  switch(cmd) {
    case __SYNC_CMD_RWSEM_RLOCK:
      rwsem_down_read_intr(&urwsem->krw_sem);
      return task_was_interrupted(caller) ? ERR(-EINTR) : 0;
    case __SYNC_CMD_RWSEM_RUNLOCK:
      rwsem_up_read(&urwsem->krw_sem);
      break;
    case __SYNC_CMD_RWSEM_WLOCK:
      rwsem_down_write_intr(&urwsem->krw_sem);
      return task_was_interrupted(caller) ? ERR(-EINTR) : 0;
    case __SYNC_CMD_RWSEM_WUNLOCK:
      rwsem_up_write(&urwsem->krw_sem);
      break;
    case __SYNC_CMD_RWSEM_TRY_RLOCK:
      return rwsem_try_down_read_intr(&urwsem->krw_sem) ? 0 : ERR(-EBUSY);
    case __SYNC_CMD_RWSEM_TRY_WLOCK:
      return rwsem_try_down_write_intr(&urwsem->krw_sem) ? 0 : ERR(-EBUSY);
    default:
      return -EINVAL;
  }
  return 0;
}

static struct __kern_sync_object *__rwsem_clone(struct __kern_sync_object *obj) {
  sync_urw_semaphore_t *new=__allocate_urw_semaphore();
  sync_urw_semaphore_t *source=(sync_urw_semaphore_t *)obj;

  if( new ) {
    new->k_syncobj.id=source->k_syncobj.id;
    return &new->k_syncobj;
  }
  return NULL;
}

static sync_obj_ops_t __rwsem_ops = {
  .control=__rwsem_control,
  .dtor=sync_default_dtor,
  .clone=__rwsem_clone,
};

static sync_urw_semaphore_t *__allocate_urw_semaphore(void)
{
  sync_urw_semaphore_t *urwsem=memalloc(sizeof(*urwsem));

  if( urwsem ) {
    memset(urwsem,0,sizeof(*urwsem));
    urwsem->k_syncobj.type=__SO_RWSEMAPHORE;
    urwsem->k_syncobj.ops=&__rwsem_ops;
    atomic_set(&urwsem->k_syncobj.refcount,1);
    rwsem_initialize(&urwsem->krw_sem);
  }
  return urwsem;
}

int sync_create_urw_semaphore(kern_sync_object_t **obj,void *uobj,
                              void *attrs,ulong_t flags)
{
  sync_urw_semaphore_t *rw_sem=__allocate_urw_semaphore();

  if( !rw_sem ) {
    return ERR(-ENOMEM);
  }

  *obj=(kern_sync_object_t*)rw_sem;
  return 0;
}
