#include <eza/arch/types.h>
#include <eza/def_actions.h>
#include <config.h>
#include <eza/smp.h>
#include <eza/spinlock.h>
#include <eza/task.h>
#include <eza/arch/bitwise.h>
#include <eza/arch/current.h>

static percpu_def_actions_t cpu_actions[CONFIG_NRCPUS];

void initialize_deffered_actions(void)
{
  ulong_t i;

  for(i=0;i<CONFIG_NRCPUS;i++) {
    percpu_def_actions_t *a=&cpu_actions[i];
    a->num_actions=0;
    list_init_head(&a->pending_actions);
    spinlock_initialize(&a->lock);
  }
}

void schedule_deffered_action(deffered_irq_action_t *a) {
  percpu_def_actions_t *acts=&cpu_actions[cpu_id()];
  long is,old_acts;

  spinlock_lock_irqsave(&acts->lock,is);
  old_acts=acts->num_actions;

  if( a->flags & __DEF_ACT_SINGLETON_MASK ) { /* Singleton. */
    if( !arch_bit_test_and_set(&a->flags,__DEF_ACT_PENDING_BIT_IDX) ) {
      if( list_is_empty(&acts->pending_actions) ) {
        list_add2tail(&acts->pending_actions,&a->node);
      } else {
        list_node_t *next=acts->pending_actions.head.next,*prev=NULL;
        deffered_irq_action_t *da;
        bool inserted=false;

        do {
          da=container_of(next,deffered_irq_action_t,node);
          if( da->target->static_priority > a->target->static_priority ) {
            break;
          } else if( da->target->static_priority == a->target->static_priority ) {
            list_add2tail(&da->head,&a->node);
            inserted=true;
            break;
          }
          prev=next;
          next=next->next;
        } while( next != list_head(&acts->pending_actions) );

        if( !inserted ) {
          if( prev != NULL ) {
            a->node.next=prev->next;
            prev->next->prev=&a->node;
            prev->next=&a->node;
            a->node.prev=prev;
          } else {
            list_add2head(&acts->pending_actions,&a->node);
          }
        }
      }
      acts->num_actions++;
    }
  } else {
  }

  if( acts->num_actions != old_acts ) {
    //arch_sched_set_def_works_pending();
  }
  spinlock_unlock_irqrestore(&acts->lock,is);
}

void fire_deffered_actions(void)
{
  percpu_def_actions_t *acts=&cpu_actions[cpu_id()];
  long is;
  deffered_irq_action_t *action;
  task_t *current;

  do {
    action=NULL;

    spinlock_lock_irqsave(&acts->lock,is);
    if( acts->num_actions ) {
      action=container_of(list_node_first(&acts->pending_actions),
                          deffered_irq_action_t,node);

//      if( (current=current_task())->priority > action->target->static_priority ) {

      if( !list_is_empty(&action->head) ) {
          list_node_t *next;

          next=action->head.head.next;

          action->head.head.prev->next=next;
          next->prev=action->head.head.prev;

          list_add2head(&acts->pending_actions,next);
      }

      if( !acts->num_actions ) {
        //arch_sched_set_def_works_pending();
      }

      acts->num_actions--;
      list_del(&action->node);
    }
//      }

    spinlock_unlock_irqrestore(&acts->lock,is);

    if( action ) {
      kprintf(" * ACTION: %p, PRIO: %d\n",action,
              action->target->static_priority);

      /* Process action. */
      switch( action->type ) {
        case DEF_ACTION_EVENT:
          break;
        case DEF_ACTION_SIGACTION:
          break;
      }
    }

  } while(action);
}
