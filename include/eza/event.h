#ifndef __EVENT_H__
#define  __EVENT_H__

#include <eza/arch/types.h>
#include <eza/spinlock.h>
#include <eza/task.h>
#include <eza/scheduler.h>

#define EVENT_OCCURED  0x1

typedef struct __event_t {
  spinlock_t __lock;
  task_t *task;
  ulong_t flags;
} event_t;

static inline void event_initialize(event_t *event)
{
  spinlock_initialize( &event->__lock, "" );
  event->flags = 0;
  event->task = NULL;
}

static inline void event_set_task(event_t *event,task_t *task)
{
  spinlock_lock(&event->__lock);
  event->task = task;
  spinlock_unlock(&event->__lock);
}

static bool event_lazy_sched_handler(void *data)
{
  event_t *t = (event_t*)data;
  return !(t->flags & EVENT_OCCURED);
}

static inline void event_yield(event_t *event)
{
  task_t *t;

  spinlock_lock(&event->__lock);
  t = event->task;
  spinlock_unlock(&event->__lock);

  if( t != NULL ) {
    sched_change_task_state_lazy(t,TASK_STATE_SLEEPING,
                                 event_lazy_sched_handler,event);
  }
}

#endif
