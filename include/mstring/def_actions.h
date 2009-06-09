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
 * (c) Copyright 2006,2007,2008 MString Core Team <http://mstring.jarios.org>
 * (c) Copyright 2008 Michael Tsymbalyuk <mtzaurus@gmail.com>
 *
 * include/mstring/def_actions.h: Data structures and prototypes for IRQ deferred actions.
 */

#ifndef __DEF_ACTIONS__
#define  __DEF_ACTIONS__

#include <arch/types.h>
#include <ds/list.h>
#include <sync/spinlock.h>
#include <mstring/event.h>
#include <mstring/siginfo.h>
#include <mstring/task.h>
#include <mstring/scheduler.h>

#define __DA_BITMASK_SIZE  (SCHED_PRIO_MAX/sizeof(long)*8)

typedef uint16_t da_counter_t;

typedef struct __percpu_def_actions {
  list_head_t pending_actions;
  ulong_t executers;
  spinlock_t lock;
} percpu_def_actions_t;

typedef enum __def_action_type {
  DEF_ACTION_EVENT,
  DEF_ACTION_SIGACTION,
  DEF_ACTION_UNBLOCK,
} def_action_type_t;

#define __DEF_ACT_FIRED_BIT_IDX     0
#define __DEF_ACT_SINGLETON_BIT_IDX   16

typedef enum {
  __DEF_ACT_FIRED_MASK=(1<<__DEF_ACT_FIRED_BIT_IDX),
  __DEF_ACT_SINGLETON_MASK=(1<<__DEF_ACT_SINGLETON_BIT_IDX),
} def_action_masks_t;

typedef struct __deffered_irq_action {
  list_node_t node; /* Must be first since it eliminites extra 'container_of'*/
  def_action_type_t type;
  list_head_t head;
  ulong_t priority;
  void *kern_priv;
  percpu_def_actions_t *__host;

  union {
    event_t _event;         /* DEF_ACTION_EVENT */
    ksiginfo_t siginfo;     /* DEF_ACTION_SIGACTION */
    task_t *target;         /* DEF_ACTION_UNBLOCK */
  } d;
} deffered_irq_action_t;

#define DEFFERED_ACTION_INIT(da,t,f)    do {    \
  list_init_head(&(da)->head);                   \
  list_init_node(&(da)->node);                   \
  (da)->type=(t);                                \
  (da)->__host=NULL;                             \
  (da)->kern_priv=NULL;                          \
  } while(0)

void initialize_deffered_actions(void);

void schedule_deffered_action(deffered_irq_action_t *a);
void fire_deffered_actions(void);
void execute_deffered_action(deffered_irq_action_t *a);
void schedule_deffered_actions(list_head_t *actions);
void deschedule_deffered_action(deffered_irq_action_t *a);
void update_deferred_actions(void);

#endif
