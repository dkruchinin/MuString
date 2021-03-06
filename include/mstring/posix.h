#ifndef __POSIX_H__
#define __POSIX_H__

#include <ds/rbtree.h>
#include <ds/idx_allocator.h>
#include <mstring/siginfo.h>
#include <mstring/signal.h>
#include <sync/mutex.h>
#include <ds/list.h>
#include <arch/atomic.h>
#include <arch/bitwise.h>
#include <mstring/timer.h>

typedef long posixid_t;

#define CONFIG_POSIX_MAX_TIMERS  128
#define CONFIG_POSIX_HASH_GROUPS  8

#define CONFIG_POSIX_MAX_OBJECTS  (CONFIG_POSIX_MAX_TIMERS * 4)

/* Structures that defines all POSIX-related stuff. */
typedef struct __posix_stuff {
  idx_allocator_t posix_ids;
  atomic_t use_counter;
  long timers;
  list_head_t list_hash[CONFIG_POSIX_HASH_GROUPS];
} posix_stuff_t;

typedef enum __posix_obj_type {
  POSIX_OBJ_TIMER,
} posix_obj_type_t;

typedef struct __posix_kern_obj {
  posix_obj_type_t type;
  list_node_t l;
  atomic_t use_counter;
  int objid,flags;
} posix_kern_obj_t;

typedef struct __posix_timer {
  posix_kern_obj_t kpo;  /* Must be first ! */
  ktimer_t ktimer;
  ulong_t interval,overrun;
} posix_timer_t;

#define __POSIX_OBJ_ACTIVE_BIT  0  /**< Target object is in active state */

#define posix_kobj_active(o)      (arch_bit_test(&((posix_kern_obj_t *)(o))->flags,__POSIX_OBJ_ACTIVE_BIT))
#define activate_posix_kobj(o)    (arch_bit_set(&((posix_kern_obj_t *)(o))->flags,__POSIX_OBJ_ACTIVE_BIT))
#define deactivate_posix_kobj(o)  (arch_bit_clear(&((posix_kern_obj_t *)(o))->flags,__POSIX_OBJ_ACTIVE_BIT))

#define posix_timer_active(t)      (posix_kobj_active((t))) 
#define deactivate_posix_timer(t)  deactivate_posix_kobj((t))
#define activate_posix_timer(t)    activate_posix_kobj((t))

#define LOCK_POSIX_STUFF_W(p)
#define UNLOCK_POSIX_STUFF_W(p)

#define LOCK_POSIX_STUFF_R(p)
#define UNLOCK_POSIX_STUFF_R(p)

#define POSIX_KOBJ_INIT(pko,t,id)               \
  (pko)->type=(t);                              \
       list_init_node(&(pko)->l);               \
       (pko)->objid=(id);                       \
       atomic_set(&(pko)->use_counter,1)

static inline int posix_allocate_obj_id(posix_stuff_t *stuff)
{
  return idx_allocate(&stuff->posix_ids);
}

static inline void posix_free_obj_id(posix_stuff_t *stuff,ulong_t id)
{
  return idx_free(&stuff->posix_ids,id);
}

posix_stuff_t *get_task_posix_stuff(task_t *task);
void release_task_posix_stuff(posix_stuff_t *stuff);

#define get_posix_object(pko)
#define release_posix_object(pko)

#define release_posix_timer(pt)

static inline void posix_insert_object(posix_stuff_t *stuff,
                                       posix_kern_obj_t *obj,ulong_t id)
{
  list_head_t *lh=&stuff->list_hash[id/(CONFIG_POSIX_MAX_OBJECTS/CONFIG_POSIX_HASH_GROUPS)];
  list_add2tail(lh,&obj->l);
}

static posix_kern_obj_t *__posix_locate_object(posix_stuff_t *stuff,ulong_t id,
                                               posix_obj_type_t type)
{
  list_head_t *lh=&stuff->list_hash[id/(CONFIG_POSIX_MAX_OBJECTS/CONFIG_POSIX_HASH_GROUPS)];  
  posix_kern_obj_t *obj;
  list_node_t *ln;

  if( id >= CONFIG_POSIX_MAX_OBJECTS ) {
    return NULL;
  }

  list_for_each(lh,ln) {
    obj=container_of(ln,posix_kern_obj_t,l);
    if( obj->objid == id && obj->type == type ) {
      get_posix_object(obj);
      goto found;
    }
  }
  obj=NULL;
found:
  return obj;
}

static inline posix_timer_t *posix_lookup_timer(posix_stuff_t *stuff,ulong_t id)
{
  posix_timer_t *t;

  LOCK_POSIX_STUFF_R(stuff);
  t=(posix_timer_t *)__posix_locate_object(stuff,id,POSIX_OBJ_TIMER);
  UNLOCK_POSIX_STUFF_R(stuff);
  return t;
}

posix_stuff_t *allocate_task_posix_stuff(void);
#define posix_validate_sigevent(se) ( ((se)->sigev_notify == SIGEV_SIGNAL || \
                                       (se)->sigev_notify == SIGEV_SIGNAL_THREAD) && \
                                      valid_signal((se)->sigev_signo) )

#endif
