#include <eza/arch/types.h>
#include <eza/def_actions.h>
#include <config.h>

static percpu_def_actions_t cpu_actions[CONFIG_NRCPUS];

void initialize_deffered_actions(void)
{
  ulong_t i;

  for(i=0;i<CONFIG_NRCPUS;i++) {
    percpu_def_actions_t *a=&cpu_actions[i];

    a->flags=0;
    list_init_head(&a->pending_actions);
    spinlock_initialize(&a->lock);
  }
}

void init_deffered_irq_action(deffered_irq_action_t *a)
{
  a->action=NULL;
  a->flags=0;  
  a->priv_data=0;
}

void schedule_deffered_action(deffered_irq_action_t *a)
{
}
