/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * (c) Copyright 2006,2007,2008 MString Core Team <http://mstring.berlios.de>
 * (c) Copyright 2008 Michael Tsymbalyuk <mtzaurus@gmail.com>
 *
 * eza/generic_api/def_actions.c: implementation of prioritized, preemptible IRQ
 *                                deferred skiplist-based actions.
 */

#include <eza/arch/types.h>
#include <eza/def_actions.h>
#include <config.h>
#include <eza/smp.h>
#include <eza/spinlock.h>
#include <eza/task.h>
#include <eza/arch/bitwise.h>
#include <eza/arch/current.h>
#include <eza/event.h>
#include <eza/arch/bits.h>

static percpu_def_actions_t cpu_actions[CONFIG_NRCPUS];

void initialize_deffered_actions(void)
{
  ulong_t i;

  for(i=0;i<CONFIG_NRCPUS;i++) {
    percpu_def_actions_t *a=&cpu_actions[i];
    memset(a,0,sizeof(*a));
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

        /* Update pending bitmask. */
        set_and_test_bit_mem(acts->pending_actions_bitmap,
                             a->target->static_priority);

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

        a->host=acts;

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

  /* Current task needs to be rescheduled as soon as possible. */
  if( acts->num_actions != old_acts &&
      a->target->static_priority <= current_task()->priority ) {
    arch_sched_set_def_works_pending();
  }
  spinlock_unlock_irqrestore(&acts->lock,is);
}

void execute_deffered_action(deffered_irq_action_t *a)
{
  switch( a->type ) {
    case DEF_ACTION_EVENT:
      event_raise(&a->d._event);
      break;
    case DEF_ACTION_SIGACTION:
      break;
  }

  arch_bit_clear(&a->flags,__DEF_ACT_PENDING_BIT_IDX);
}

void fire_deffered_actions(void)
{
  percpu_def_actions_t *acts=&cpu_actions[cpu_id()];
  long is;
  deffered_irq_action_t *action;

  /* To prevent recursive invocations. */
  spinlock_lock_irqsave(&acts->lock,is);
  if( acts->executers || !acts->num_actions ) {
    preempt_enable();
    spinlock_unlock_irqrestore(&acts->lock,is);
    return;
  } else {
    acts->executers++;
  }
  spinlock_unlock_irqrestore(&acts->lock,is);

  do {
    action=NULL;

    spinlock_lock_irqsave(&acts->lock,is);
    if( acts->num_actions ) {
      action=container_of(list_node_first(&acts->pending_actions),
                          deffered_irq_action_t,node);

      if( current_task()->priority >= action->target->static_priority &&
          action->target->static_priority <= find_first_bit_mem(acts->fired_actions_bitmap,
                                                                __DA_BITMASK_SIZE) ) {
        list_node_t *prev=action->node.prev;

        /* Mark this action as 'fired'. */
        set_and_test_bit_mem(acts->fired_actions_bitmap,action->target->static_priority);
        acts->fired_actions_counters[action->target->static_priority]++;

        list_del(&action->node);
        if( !list_is_empty(&action->head) ) {
          deffered_irq_action_t *a=container_of(list_node_first(&action->head),
                                                deffered_irq_action_t,node);
          list_del(&a->node);

          if( !list_is_empty(&action->head) ) {
            list_move2head(&a->head,&action->head);
          }

          a->node.prev=prev;
          a->node.next=prev->next;
          prev->next->prev=&a->node;
          prev->next=&a->node;
        }

        acts->num_actions--;
        action->host=NULL;
        arch_sched_set_def_works_pending();
      } else {
        /* Bad luck - current thread has higher priority than any of pending
         * deffered actions.
         */
        action=NULL;
      }
    }

    if( !action ) { /* No valid actions found. */
      arch_sched_reset_def_works_pending();
    }
    spinlock_unlock_irqrestore(&acts->lock,is);

    if( action ) {
      execute_deffered_action(action);
    }
  } while(action != NULL);

  spinlock_lock_irqsave(&acts->lock,is);
  acts->executers--;
  preempt_enable();   /* Trigger preemption. */
  spinlock_unlock_irqrestore(&acts->lock,is);
}
