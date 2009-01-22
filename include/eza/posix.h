#ifndef __POSIX_H__
#define __POSIX_H__

#include <ds/rbtree.h>
#include <ds/idx_allocator.h>
#include <eza/siginfo.h>
#include <eza/signal.h>
#include <eza/mutex.h>
#include <ds/list.h>

typedef long posixid_t;

/* Structures that defines all POSIX-related stuff. */
typedef struct __posix_stuff {
  mutex_t mutex;
  idx_allocator_t __posix_ids;
  struct rb_root __posix_objects;
} posix_stuff_t;

typedef enum __posix_obj_type {
  POSIX_OBJ_TIMER,
} posix_obj_type_t;

typedef struct __posix_kern_obj {
  posix_obj_type_t type;
  struct rb_node rb_node;
  list_node_t l;
} posix_kern_obj_t;

typedef struct __posix_timer {
  posix_kern_obj_t kpo;
  struct sigevent sevent;
} posix_timer_t;

#define LOCK_POSIX_STUFF_W(p)
#define UNLOCK_POSIX_STUFF_W(p)

#define LOCK_POSIX_STUFF_R(p)
#define UNLOCK_POSIX_STUFF_R(p)

#define POSIX_KOBJ_INIT(pko,t)                  \
  (pko)->type=(t);                              \
       list_init_node(&(pko)->l)

long posix_allocate_obj_id(posix_stuff_t *stuff);
void posix_free_obj_id(posix_stuff_t *stuff,long id);

posix_stuff_t *get_task_posix_stuff(task_t *task);
void release_task_posix_stuff(posix_stuff_t *stuff);
void posix_insert_object(posix_stuff_t *stuff,posix_kern_obj_t *obj,
                         ulong_t id);

#define posix_validate_sigevent(se) ( ((se)->sigev_notify == SIGEV_SIGNAL) && \
                                      valid_signal((se)->sigev_signo) )

#endif
