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
 * eza/sync/topevel.c: Core logic for userspace synchronization subsystem.
 */

#include <eza/arch/types.h>
#include <eza/mutex.h>
#include <eza/sync.h>
#include <kernel/vm.h>
#include <eza/task.h>
#include <mm/slab.h>

static status_t __sync_allocate_id(struct __task_struct *task,sync_id_t *p_id)
{
  static sync_id_t __id=0;

  *p_id=++__id;
  return 0;
}

static void __sync_free_id(struct __task_struct *task,sync_id_t id)
{
}

static void __install_sync_object(task_t *task,kern_sync_object_t *obj,
                                  sync_id_t id)
{
  obj->id=id;
}

static kern_sync_object_t *__lookup_sync(task_t *owner,sync_id_t id)
{
  kern_sync_object_t *t;

  return t;
}

status_t sys_sync_create_object(sync_object_type_t obj_type,
                                ulong_t flags)
{
  kern_sync_object_t *obj;
  status_t r;
  sync_id_t id;
  task_t *caller=current_task();

  if( obj_type > __SO_MAX_TYPE ) {
    return -EINVAL;
  }

  r=__sync_allocate_id(caller,&id);
  if( r ) {
    return r;
  }

  switch( obj_type ) {
    case __SO_MUTEX:
      r=sync_create_mutex(&obj,flags);
      break;
    case __SO_CONDVAR:
    case __SO_SEMAPHORE:
    default:
      r=-EINVAL;
      break;
  }

  if( r ) {
    goto put_id;
  }

  __install_sync_object(caller,obj,id);
  return obj->id;
put_id:
  __sync_free_id(caller,id);
  return r;
}

status_t sys_sync_control(sync_id_t id,ulong_t cmd,ulong_t arg)
{
  task_t *caller=current_task();
  kern_sync_object_t *kobj=__lookup_sync(caller,id);
  status_t r;

  if( !kobj ) {
    return -EINVAL;
  }

  r=kobj->ops->control(kobj,cmd,arg);
  sync_put_object(kobj);
  return r;
}

status_t sys_sync_condvar_wait(sync_id_t ucondvar,
                               sync_id_t umutex)
{
  return 0;
}

status_t dup_task_sync_data(task_sync_data_t *sync_data)
{
  ulong_t i;

  atomic_inc(&sync_data->use_count);
  LOCK_SYNC_DATA(sync_data);

  for(i=0;i<MAX_PROCESS_SYNC_OBJS;i++) {
  }

  UNLOCK_SYNC_DATA(sync_data);
  return 0;
}

task_sync_data_t *allocate_task_sync_data(void)
{
  task_sync_data_t *s=memalloc(sizeof(*s));

  if( s ) {
    memset(s,0,sizeof(*s));
    atomic_set(&s->use_count,1);
    mutex_initialize(&s->mutex);
  }

  return s;
}
