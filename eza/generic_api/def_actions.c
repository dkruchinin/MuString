#include <eza/arch/types.h>
#include <eza/def_actions.h>

void init_deffered_irq_action(deffered_irq_action_t *a)
{
  a->action=NULL;
  a->flags=0;  
  a->priv_data=0;
}

void schedule_deffered_action(deffered_irq_action_t *a)
{
}
