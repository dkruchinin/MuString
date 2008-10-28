#ifndef __EVENT_H__
#define  __EVENT_H__

#include <eza/arch/types.h>
#include <eza/spinlock.h>
#include <eza/task.h>
#include <eza/scheduler.h>

#define EVENT_OCCURED  0x1

#define LOCK_EVENT(e)                           \
    interrupts_disable();                       \
    spinlock_lock(&e->__lock)

#define UNLOCK_EVENT(e)                         \
    spinlock_unlock(&e->__lock);                \
    interrupts_enable();

typedef bool (*event_checker_t)(void *priv);

typedef struct __event_t {
  spinlock_t __lock;
  task_t *task;
  ulong_t flags;
  void *private_data;
  event_checker_t ev_checker;
} event_t;

static inline void event_initialize(event_t *event)
{
  spinlock_initialize( &event->__lock, "" );
  event->flags=0;
  event->task=NULL;
  event->ev_checker=NULL;
  event->private_data=NULL;
}

static inline void event_set_checker(event_t *event,
                                     event_checker_t checker,void *data)
{
  LOCK_EVENT(event);
  event->ev_checker=checker;
  event->private_data=data;
  UNLOCK_EVENT(event);
}

static inline void event_set_task(event_t *event,task_t *task)
{
  LOCK_EVENT(event);
  event->task = task;
  UNLOCK_EVENT(event);
}

static bool event_lazy_sched_handler(void *data)
{
  event_t *t = (event_t*)data;

  if( t->ev_checker ) {
    return t->ev_checker(t->private_data);
  }

  return !(t->flags & EVENT_OCCURED);
}

static inline void event_yield(event_t *event)
{
  task_t *t;

  LOCK_EVENT(event);
  t = event->task;
  UNLOCK_EVENT(event);

  if( t != NULL ) {
      event_checker_t ec=event->ev_checker;

      if(!ec) {
        ec=event_lazy_sched_handler;
      }
      sched_change_task_state_lazy(t,TASK_STATE_SLEEPING,ec,event);

      if( !event->ev_checker ) {
        event->flags &= ~EVENT_OCCURED;
      }
  }
}

static inline void event_raise(event_t *event)
{
  task_t *t;

  LOCK_EVENT(event);
  if( !event->ev_checker ) {
    if( !(event->flags & EVENT_OCCURED) ) {
      event->flags |= EVENT_OCCURED;
      t=event->task;
    } else {
      t=NULL;
    }
  } else {
    t=event->task;
  }
  UNLOCK_EVENT(event);

  if( t != NULL) {
    sched_change_task_state(t,TASK_STATE_RUNNABLE);    
  }
}

#endif
