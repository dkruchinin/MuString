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
 * include/eza/def_actions.h: Data structures and prototypes for IRQ deferred actions.
 */

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
  ulong_t num_actions,executers;
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
  (da).host=NULL;                               \
  } while(0)

void initialize_deffered_actions(void);

void schedule_deffered_action(deffered_irq_action_t *a);
void fire_deffered_actions(void);
void execute_deffered_action(deffered_irq_action_t *a);

#endif
