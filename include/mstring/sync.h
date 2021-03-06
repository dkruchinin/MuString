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
 * include/mstring/sync.h: Data types and prototypes for for userspace
 *                     synchronization subsystem.
 */

#ifndef __SYNC_H__
#define  __SYNC_H__

#include <arch/types.h>
#include <sync/mutex.h>
#include <arch/atomic.h>
#include <mstring/stddef.h>
#include <mstring/task.h>
#include <sync/mutex.h>
#include <mm/slab.h>
#include <mstring/waitqueue.h>
#include <sync/spinlock.h>
#include <sync/rwsem.h>

#define SYNC_OBJS_PER_PROCESS   16384
#define SYNC_ID_NOT_INITIALIZED 0

/* Common flags for sync objects. */
#define __SO_ATTR_PTHREAD_PROCESS_SHARED  0x1
#define __SO_ATTR_PTHREAD_PROCESS_PRIVATE  0x2

typedef ulong_t sync_id_t;

typedef enum __sync_object_type {
  __SO_MUTEX=0,  /**< Userspace mutex **/
  __SO_SEMAPHORE=1, /**< Userspace semaphore **/
  __SO_CONDVAR=2, /**< Userspace conditional variable **/

  __SO_RAWEVENT=3, /**< Raw userspace event **/
  __SO_RWSEMAPHORE=4, /**< R/W semaphore **/
  /* XXX: Update __SO_MAX_TYPE in case a new sync type appears. */
} sync_object_type_t;

#define __SO_MAX_TYPE  __SO_RWSEMAPHORE

struct __kern_sync_object;

typedef struct __sync_obj_ops {
  int (*control)(struct __kern_sync_object *obj,ulong_t cmd,ulong_t arg);
  void (*dtor)(void *obj);
  struct __kern_sync_object *(*clone)(struct __kern_sync_object *obj);
} sync_obj_ops_t;

typedef struct __uspace_mutex {  mutex_t mutex;
  ulong_t flags;
} uspace_mutex_t;

typedef struct __kern_sync_object {
  sync_id_t id;
  sync_object_type_t type;
  sync_obj_ops_t *ops;
  atomic_t refcount;
} kern_sync_object_t;

enum {
  PTHREAD_PROCESS_PRIVATE=0,  /**< Private primitive. **/
  PTHREAD_PROCESS_SHARED=1    /**< Shared primitive. **/
};

/* Per-process sync info structure. */
#define SYNC_OBJS_PAGES 4
#define MAX_PROCESS_SYNC_OBJS  ((PAGE_SIZE * SYNC_OBJS_PAGES - (sizeof(atomic_t)+2*sizeof(ulong_t)+sizeof(mutex_t))) / sizeof(long))

typedef struct __task_sync_data {
  atomic_t use_count;
  ulong_t numobjs;
  mutex_t mutex;
  kern_sync_object_t *sync_objects[MAX_PROCESS_SYNC_OBJS];
} task_sync_data_t;

struct __task_struct;

static inline void sync_put_object(kern_sync_object_t *obj)
{
  if( atomic_dec_and_test(&obj->refcount) ) {
    obj->ops->dtor(obj);
  }
}

#define sync_get_object(o)  atomic_inc(&(o)->refcount)

static inline task_sync_data_t *get_task_sync_data(task_t *t)
{
  task_sync_data_t *sync_data;

  LOCK_TASK_MEMBERS(t);
  if( t->sync_data ) {
    atomic_inc(&t->sync_data->use_count);
    sync_data=t->sync_data;
  } else {
    sync_data=NULL;
  }
  UNLOCK_TASK_MEMBERS(t);

  return sync_data;
}

static inline void __free_task_sync_data(task_sync_data_t *sync_data)
{
  free_pages_addr(sync_data,SYNC_OBJS_PAGES);
}

static inline void release_task_sync_data(task_sync_data_t *sync_data)
{
  if( atomic_dec_and_test(&sync_data->use_count) ) {
    __free_task_sync_data(sync_data);
  }
}

task_sync_data_t *allocate_task_sync_data(void);
task_sync_data_t *replicate_task_sync_data(struct __task_struct *parent);
int dup_task_sync_data(task_sync_data_t *sync_data);

#define LOCK_SYNC_DATA_R(l)  mutex_lock(&(l)->mutex)
#define UNLOCK_SYNC_DATA_R(l)  mutex_unlock(&(l)->mutex)

#define LOCK_SYNC_DATA_W(l)  mutex_lock(&(l)->mutex)
#define UNLOCK_SYNC_DATA_W(l)  mutex_unlock(&(l)->mutex)


/* Common control commands. */
enum {
  __SYNC_CMD_DESTROY_OBJECT=0x10,  /**< Destroy target primitive. **/
};

/* Common, well-known object attributes. */
enum {
  __SYNC_OBJ_ATTR_SHARED_BIT=0,
};

enum {
  __SYNC_OBJ_ATTR_SHARED_MASK=(1<<__SYNC_OBJ_ATTR_SHARED_BIT),
};

#define shared_object(a) (((a) & __SYNC_OBJ_ATTR_SHARED_MASK ) ? true : false )

void sync_default_dtor(void *obj); /**< Default object destructor. **/

/* Userspace mutexes-related stuff. */
typedef struct __sync_umutex {
  kern_sync_object_t k_syncobj;
  mutex_t __kmutex;
} sync_umutex_t;

typedef enum __umutex_commands {
  __SYNC_CMD_MUTEX_LOCK=0x100,    /**< Lock target mutex. **/
  __SYNC_CMD_MUTEX_UNLOCK=0x101,  /**< Unlock target mutex. **/
  __SYNC_CMD_MUTEX_TRYLOCK=0x102, /**< Try to lock target mutex. **/
} umutex_commands_t;

typedef struct __pthread_mutexattr {
  uint8_t __type;
} pthread_mutexattr_t;

typedef struct {
  sync_id_t __id;
  pthread_mutexattr_t __attrs;
} pthread_mutex_t;

#define __UMUTEX_OBJ(ksync_obj) (container_of((ksync_obj),      \
                                              sync_umutex_t,k_syncobj))

int sync_create_mutex(kern_sync_object_t **obj,void *uobj,
                      uint8_t *attrs,ulong_t flags);

/* Userspace raw sync events - related stuff. */
typedef struct __sync_uevent {
  kern_sync_object_t k_syncobj;
  wqueue_t __wq;
  spinlock_t __lock;
  ulong_t __ecount;
} sync_uevent_t;

int sync_create_uevent(kern_sync_object_t **obj,void *uobj,
                       uint8_t *attrs,ulong_t flags);

enum {
  __SYNC_CMD_EVENT_WAIT=0x200,       /**< Wait for target event to arrive. **/
  __SYNC_CMD_EVENT_SIGNAL=0x201,     /**< Signal target event. **/
  __SYNC_CMD_EVENT_BROADCAST=0x202,  /**< Signal broadcust event */
  __SYNC_CMD_EVENT_TIMEDWAIT =0x203, /**< Wait for target event given period of tieme */
};

#define __UEVENT_OBJ(ksync_obj) (container_of((ksync_obj),      \
                                              sync_uevent_t,k_syncobj))

/* Userspace RW semaphores - related stuff. */
typedef struct __sync_urw_semaphore {
  kern_sync_object_t k_syncobj;
  rwsem_t krw_sem;
} sync_urw_semaphore_t;

int sync_create_urw_semaphore(kern_sync_object_t **obj,void *uobj,
                              void *attrs,ulong_t flags);

enum {
  __SYNC_CMD_RWSEM_RLOCK=0x300,       /**< Grab semaphore as a reader. **/
  __SYNC_CMD_RWSEM_RUNLOCK=0x301,     /**< Release semaphore as a reader. **/
  __SYNC_CMD_RWSEM_WLOCK=0x302,       /**< Grab semaphore as a writer. **/
  __SYNC_CMD_RWSEM_WUNLOCK=0x303,     /**< Release semaphore as a writer. **/
  __SYNC_CMD_RWSEM_TRY_RLOCK=0x304,   /**< Try to grab a semaphore as a reader. */
  __SYNC_CMD_RWSEM_TRY_WLOCK=0x305,   /**< Try to grab a semaphore as a writer. */
};

typedef struct {
  sync_id_t __id;
  int state;
} pthread_rwlock_t;

#define __URWSEMAPHORE_OBJ(ksync_obj) (container_of((ksync_obj),        \
                                                    sync_urw_semaphore_t,k_syncobj))

#endif
