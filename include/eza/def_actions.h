#ifndef __DEF_ACTIONS__
#define  __DEF_ACTIONS__

#include <eza/arch/types.h>
#include <ds/list.h>
#include <eza/spinlock.h>
#include <eza/event.h>
#include <eza/siginfo.h>
#include <eza/task.h>

typedef struct __percpu_def_actions {
  list_head_t pending_actions;
  ulong_t num_actions;
  spinlock_t lock;
} percpu_def_actions_t;

typedef enum __def_action_type {
  DEF_ACTION_EVENT,
  DEF_ACTION_SIGACTION,
} def_action_type_t;

#define __DEF_ACT_PENDING_BIT_IDX     0
#define __DEF_ACT_SINGLETON_BIT_IDX   16

typedef enum {
  __DEF_ACT_PENDING_MASK=(1<<__DEF_ACT_PENDING_BIT_IDX),
  __DEF_ACT_SINGLETON_MASK=(1<<__DEF_ACT_SINGLETON_BIT_IDX),
} def_action_masks_t;

typedef struct __deffered_irq_action {
  ulong_t flags;
  def_action_type_t type;
  list_head_t head;
  list_node_t node;
  task_t *target;

  union {
    event_t _event;
    struct sigevent _sigevent;
  } d;
  percpu_def_actions_t *host;
} deffered_irq_action_t;

#define DEFFERED_ACTION_INIT(da,t,f)    do {    \
  list_init_head(&(da).head);                   \
  list_init_node(&(da).node);                   \
  (da).target=NULL;                             \
  (da).type=(t);                                \
  (da).flags=(f);                               \
  } while(0)

void initialize_deffered_actions(void);

void schedule_deffered_action(deffered_irq_action_t *a);
void fire_deffered_actions(void);

#endif
