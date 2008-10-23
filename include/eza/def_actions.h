#ifndef __DEF_ACTIONS__
#define  __DEF_ACTIONS__

#include <eza/arch/types.h>

#define DEF_IRQ_ACTION_ACTIVE  0x1

typedef void (*def_action_t)(ulong_t data);

typedef struct __deffered_irq_action {
  ulong_t flags;
  def_action_t action;
  ulong_t priv_data;
} deffered_irq_action_t;

void init_deffered_irq_action(deffered_irq_action_t *a);
void schedule_deffered_action(deffered_irq_action_t *a);

#endif
