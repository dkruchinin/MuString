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
  ulong_t flags,waitcnt;
  void *private_data;
  event_checker_t ev_checker;
} event_t;

typedef struct __countered_event {
  atomic_t counter;
  event_t e;
} countered_event_t;

#define event_is_active(e)  ((e)->task != NULL)

static inline void event_initialize_task(event_t *event,struct __task_struct *task)
{
  spinlock_initialize(&event->__lock);
  event->flags=event->waitcnt=0;
  event->task=task;
  event->ev_checker=NULL;
  event->private_data=NULL;
}

#define event_initialize(_e)  event_initialize_task((_e),NULL)

static inline void event_reset(event_t *event)
{
  long is;

  spinlock_lock_irqsave(&event->__lock,is);
  event->flags=event->waitcnt=0;
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
  bool c;

  if( t->ev_checker ) {
    c=t->ev_checker(t->private_data);
  } else {
    c=!(t->flags & EVENT_OCCURED);
  }

  if( c ) {
    t->waitcnt = 1;
  }
  return c;
}

static inline int event_yield_state(event_t *event,int state)
{
  struct __task_struct *t;
  int r=-EINVAL;
  long is;

  LOCK_EVENT(event,is);
  t = event->task;
  UNLOCK_EVENT(event,is);

  if( t != NULL ) {    
    event_checker_t ec=event->ev_checker;

    if(!ec) {
      ec=event_defered_sched_handler;
    }
    r=sched_change_task_state_deferred(t,state,ec,event);

    LOCK_EVENT(event,is);
    event->waitcnt = 0;
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

#define event_yield_intr(e)  event_yield_state((e),TASK_STATE_SLEEPING)
#define event_yield_susp(e)  event_yield_state((e),TASK_STATE_SUSPENDED)
#define event_yield(e)  event_yield_intr((e))

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
    event->flags |= EVENT_OCCURED;
    if( event->waitcnt ) {
      sched_change_task_state(t,TASK_STATE_RUNNABLE);
    }
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
