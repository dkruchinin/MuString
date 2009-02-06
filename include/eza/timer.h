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
 * (c) Copyright 2008 MadTirra <tirra.newly@gmail.com>
 *
 * include/eza/timer.h: contains main kernel types and prototypes for dealing
 *                      with hardware timers.
 *
 */

#ifndef __EZA_TIMER_H__
#define __EZA_TIMER_H__

#include <config.h>
#include <ds/list.h>
#include <eza/def_actions.h>
#include <ds/rbtree.h>
#include <eza/arch/atomic.h>
#include <eza/spinlock.h>
#include <mlibc/types.h>
#include <eza/interrupt.h>

typedef struct __hw_timer_type {
  list_node_t l;
  const char *descr;
  void (*calibrate)(uint32_t hz);
  void (*resume)(void);
  void (*suspend)(void);
  void (*register_callback)(irq_t irq,irq_handler_t handler);
} hw_timer_t;

void hw_timer_register(hw_timer_t *ctrl);

typedef void (*timer_handler_t)(ulong_t data);

#define TF_TIMER_ACTIVE  0x1        /* Timer is active and ticking. */

struct __major_timer_tick;

typedef struct __timer_tick {
  ulong_t time_x;      /* Trigger time. */
  list_node_t node;    /* To link us with our major tick. */
  list_head_t actions; /* Deffered actions for this tick. */
  struct __major_timer_tick *major_tick;
} timer_tick_t;

#define TIMER_TICK_INIT(tt,tx)                    \
  (tt)->time_x=(tx);                              \
  list_init_node(&(tt)->node);                    \
  list_init_head(&(tt)->actions);                 \
  (tt)->major_tick=NULL

#define LOCK_MAJOR_TIMER_TICK(t,_is)            \
  spinlock_lock_irqsave(&(t)->lock,(_is));

#define UNLOCK_MAJOR_TIMER_TICK(t,_is)            \
  spinlock_unlock_irqrestore(&(t)->lock,(_is));

typedef struct __ktimer {
  timer_tick_t minor_tick;
  deffered_irq_action_t da;
  ulong_t time_x;
} ktimer_t;

#define MINOR_TICK_GROUP_SIZE  16
#define MINOR_TICK_GROUPS  CONFIG_TIMER_GRANULARITY / MINOR_TICK_GROUP_SIZE

typedef struct __major_timer_tick {
  spinlock_t lock;
  atomic_t use_counter;
  struct rb_node rbnode;
  ulong_t time_x;
  list_head_t minor_ticks[MINOR_TICK_GROUPS];
} major_timer_tick_t;

void init_timers(void);
void init_timer(ktimer_t *t);
long add_timer(ktimer_t *t);
void delete_timer(ktimer_t *t);
void adjust_timer(ktimer_t *t,long delta);
void process_timers(void);
void timer_cleanup_expired_ticks(void);

#define init_timer(t,tx,tp)                              \
  DEFFERED_ACTION_INIT(&(t)->da,(tp),0);                 \
  (t)->time_x=(tx);                                      \
  TIMER_TICK_INIT(&(t)->minor_tick,(t)->time_x);         \
  (t)->da.priority=current_task()->priority

#define TIMER_RESET_TIME(_t,_tx)                \
  (_t)->time_x=(_tx);                           \
  (_t)->minor_tick.time_x=(_tx)

#endif /*__EZA_TIMER_H__*/

