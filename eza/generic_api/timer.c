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
 * (c) Copyright 2008 MadTirra <tirra.newly@gmail.com>
 * (c) Copyright 2008 Michael Tsymbalyuk <mtzaurus@gmail.com>
 *
 * eza/generic_api/timer.c: contains routines for dealing with hardware
 *                          timers. 
 *
 * - added support of software timers (Michael Tsymbalyuk);
 */

#include <ds/list.h>
#include <eza/interrupt.h>
#include <eza/errno.h>
#include <eza/spinlock.h>
#include <mlibc/string.h>
#include <eza/swks.h>
#include <mlibc/kprintf.h>
#include <eza/timer.h>
#include <eza/arch/interrupt.h>
#include <eza/time.h>
#include <eza/def_actions.h>

/*spinlock*/
static spinlock_declare(timer_lock);
static spinlock_declare(sw_timers_lock);

/*list of the timers*/
static LIST_DEFINE(known_hw_timers);
static LIST_DEFINE(active_sw_timers);
static LIST_DEFINE(pending_sw_timers);
static deffered_irq_action_t timer_action;

#define GRAB_HW_TIMER_LOCK() spinlock_lock(&timer_lock)
#define RELEASE_HW_TIMER_LOCK() spinlock_unlock(&timer_lock)

#define GRAB_SW_TIMER_LOCK(l)  spinlock_lock_irqsave(&sw_timers_lock,l)
#define RELEASE_SW_TIMER_LOCK(l) spinlock_unlock_irqrestore(&sw_timers_lock,l)

#define MARK_TIMER_DEACTIVATED(t) t->flags &= ~TF_TIMER_ACTIVE

static void init_hw_timers (void)
{
  list_init_head(&known_hw_timers);
}

static void __timer_action_handler(ulong_t data)
{
  ulong_t l;
  list_node_t *first,*n;

  GRAB_SW_TIMER_LOCK(l);
  if( !list_is_empty(&pending_sw_timers) ) {
    first=list_node_first(&pending_sw_timers);
    list_cut_head(&pending_sw_timers);
  } else {
    first=NULL;
  }
  RELEASE_SW_TIMER_LOCK(l);

  if( first!=NULL ) {
    n=first;
    do {
      timer_t *t=container_of(n,timer_t,l);
      t->handler(t->data);
      MARK_TIMER_DEACTIVATED(t);
      n=n->next;
    } while( n!=first );
  }
}

static void init_sw_timers(void)
{
  list_init_head(&active_sw_timers);
  list_init_head(&pending_sw_timers);

  init_deffered_irq_action(&timer_action);
  timer_action.action=__timer_action_handler;
}

void hw_timer_register (hw_timer_t *ctrl)
{
  GRAB_HW_TIMER_LOCK();
  list_add(list_node_first(&known_hw_timers), &ctrl->l);
  RELEASE_HW_TIMER_LOCK();
}

void hw_timer_generic_suspend(void)
{
}

void init_timers(void)
{
  init_hw_timers();
  init_sw_timers();
  initialize_deffered_actions();
}

/* NOTE: timer list must be locked prior calling this function. */
static void __insert_timer(timer_t *timer)
{
  list_node_t *n;

  list_for_each(&active_sw_timers,n) {
    timer_t *t=container_of(n,timer_t,l);
    if(timer->time_x <= t->time_x) {
      list_add(n,&timer->l);
      return;
    }
  }
  /* No luck - put the timer in the end of list. */
  list_add2tail(&active_sw_timers,&timer->l);
}

bool add_timer(timer_t *timer)
{
  bool added;
  ulong_t l;

  GRAB_SW_TIMER_LOCK(l);
  if( !(timer->flags & TF_TIMER_ACTIVE) && timer->time_x>system_ticks
      && timer->handler ) {
    list_init_node(&timer->l);
    timer->flags |= TF_TIMER_ACTIVE;
    __insert_timer(timer);
    added=true;
  } else {
    added=false;
  }
  RELEASE_SW_TIMER_LOCK(l);
  return added;
}

void delete_timer(timer_t *timer)
{
  ulong_t l;

  GRAB_SW_TIMER_LOCK(l);
  if( timer->flags & TF_TIMER_ACTIVE ) {
    list_del(&timer->l);
    MARK_TIMER_DEACTIVATED(timer);
  }
  RELEASE_SW_TIMER_LOCK(l);
}

void adjust_timer(timer_t *timer,long_t delta)
{
}

void init_timer(timer_t *t)
{
  t->flags=t->time_x=0;
  list_init_node(&t->l);
  t->handler=NULL;
}

void process_timers(void)
{
  ulong_t l;
  list_node_t *n,*first,*last;

  first=last=NULL;

  GRAB_SW_TIMER_LOCK(l);
  list_for_each(&active_sw_timers,n) {
    timer_t *t=container_of(n,timer_t,l);

    if( t->time_x<=system_ticks ) {
      if( first==NULL ) {
        first=&t->l;
      }
      last=&t->l;
    }
  }

  /* Cut all pending timers. */
  if( first != NULL ) {
    list_cut_sublist(first,last);

    /* Let these timers be processed by the timer deferred task. */
    if( list_is_empty(&pending_sw_timers) ) {
      list_set_head(&pending_sw_timers,first);
    } else {
      list_add_range(first,last,list_node_last(&pending_sw_timers)->prev,
                     list_node_last(&pending_sw_timers));
    }
  }
  RELEASE_SW_TIMER_LOCK(l);

  if( !list_is_empty(&pending_sw_timers) ) {
    __timer_action_handler(0);
  }
}
