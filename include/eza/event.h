#ifndef __EVENT_H__
#define  __EVENT_H__

#include <eza/arch/types.h>
#include <eza/spinlock.h>
#include <eza/scheduler.h>
#include <eza/arch/atomic.h>
#include <eza/errno.h>

struct __task_struct;

#define EVENT_OCCURED  0x1

#define LOCK_EVENT(e,s) spinlock_lock_irqsave(&(e)->__lock,(s))

#define UNLOCK_EVENT(e,s) spinlock_unlock_irqrestore(&(e)->__lock,(s))

typedef bool (*event_checker_t)(void *priv);

typedef struct __event_t {
  spinlock_t __lock;
  struct __task_struct *task;
  ulong_t flags;
  void *private_data;
  event_checker_t ev_checker;
} event_t;

typedef struct __countered_event {
  atomic_t counter;
  event_t e;
} countered_event_t;

#define event_is_active(e)  ((e)->task != NULL)

static inline void event_initialize(event_t *event)
{
  spinlock_initialize(&event->__lock);
  event->flags=0;
  event->task=NULL;
  event->ev_checker=NULL;
  event->private_data=NULL;
}

static inline void event_reset(event_t *event)
{
  long is;

  spinlock_lock_irqsave(&event->__lock,is);
  event->flags=0;
  event->task=NULL;
  spinlock_unlock_irqrestore(&event->__lock,is);
}

static inline void event_set_checker(event_t *event,
                                     event_checker_t checker,void *data)
{
  long is;

  spinlock_lock_irqsave(&event->__lock,is);
  event->ev_checker=checker;
  event->private_data=data;
  spinlock_unlock_irqrestore(&event->__lock,is);
}

static inline void event_set_task(event_t *event,struct __task_struct *task)
{
  long is;

  spinlock_lock_irqsave(&event->__lock,is);
  event->task = task;
  spinlock_unlock_irqrestore(&event->__lock,is);
}

static bool event_defered_sched_handler(void *data)
{
  event_t *t = (event_t*)data;

  if( t->ev_checker ) {
    return t->ev_checker(t->private_data);
  }

  return !(t->flags & EVENT_OCCURED);
}

static inline int event_yield(event_t *event)
{
  struct __task_struct *t;
  status_t r=-EINVAL;
  long is;

  LOCK_EVENT(event,is);
  t = event->task;
  UNLOCK_EVENT(event,is);

  if( t != NULL ) {    
    event_checker_t ec=event->ev_checker;

    if(!ec) {
      ec=event_defered_sched_handler;
    }
    r=sched_change_task_state_deferred(t,TASK_STATE_SLEEPING,ec,event);

    LOCK_EVENT(event,is);
    if( event->flags & EVENT_OCCURED ) {
      event->flags &= ~EVENT_OCCURED;
      r=0;
    } else {
      r=1;
    }
    UNLOCK_EVENT(event,is);
  }
  return r;
}

static inline void event_raise(event_t *event)
{
  struct __task_struct *t;
  long is;
  
  LOCK_EVENT(event,is);
  if( !event->ev_checker ) {
    if( !(event->flags & EVENT_OCCURED) ) {
      t=event->task;
    } else {
      t=NULL;
    }
  } else {
    t=event->task;
  }

  if( t != NULL) {
    sched_change_task_state(t,TASK_STATE_RUNNABLE);    
    event->flags |= EVENT_OCCURED;
  }
  UNLOCK_EVENT(event,is);
}

static inline void countered_event_raise(countered_event_t *ce)
{
  if( atomic_dec_and_test(&ce->counter) ) {
    event_raise(&ce->e);
  }
}

#endif
