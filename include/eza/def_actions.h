#ifndef __DEF_ACTIONS__
#define  __DEF_ACTIONS__

#include <eza/arch/types.h>
#include <ds/list.h>
#include <eza/spinlock.h>

#define DEF_IRQ_ACTION_ACTIVE  0x1

typedef void (*def_action_t)(ulong_t data);

#define DEF_ACTIONS_DISABLED  0x1

typedef struct __percpu_def_actions {
  list_head_t pending_actions;
  spinlock_t lock;
  ulong_t flags; 
} percpu_def_actions_t;

typedef struct __deffered_irq_action {
  ulong_t flags;
  def_action_t action;
  ulong_t priv_data;
} deffered_irq_action_t;

void initialize_deffered_actions(void);
void init_deffered_irq_action(deffered_irq_action_t *a);
void schedule_deffered_action(deffered_irq_action_t *a);

#endif
