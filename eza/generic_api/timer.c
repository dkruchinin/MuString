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
#include <ds/rbtree.h>
#include <ds/skiplist.h>
#include <mlibc/types.h>

/*spinlock*/
static SPINLOCK_DEFINE(timer_lock);
static SPINLOCK_DEFINE(sw_timers_lock);
static SPINLOCK_DEFINE(sw_timers_list_lock);

/*list of the timers*/
static LIST_DEFINE(known_hw_timers);

static struct rb_root timers_rb_root;
static ulong_t __last_processed_timer_tick;

#define GRAB_HW_TIMER_LOCK() spinlock_lock(&timer_lock)
#define RELEASE_HW_TIMER_LOCK() spinlock_unlock(&timer_lock)

#define LOCK_SW_TIMERS(l)  spinlock_lock_irqsave(&sw_timers_lock,l)
#define UNLOCK_SW_TIMERS(l) spinlock_unlock_irqrestore(&sw_timers_lock,l)

#define LOCK_SW_TIMERS_R(l)  spinlock_lock_irqsave(&sw_timers_lock,l)
#define UNLOCK_SW_TIMERS_R(l) spinlock_unlock_irqrestore(&sw_timers_lock,l)

#define get_major_tick(t) atomic_inc(&(t)->use_counter)
#define put_major_tick(t) if( atomic_dec_and_test(&(t)->use_counter) ) { memfree(t); }

static void init_hw_timers (void)
{
  list_init_head(&known_hw_timers);
}

void hw_timer_register (hw_timer_t *ctrl)
{
  GRAB_HW_TIMER_LOCK();
  list_add_before(list_node_first(&known_hw_timers), &ctrl->l);
  RELEASE_HW_TIMER_LOCK();
}

void hw_timer_generic_suspend(void)
{
}

void init_timers(void)
{
  init_hw_timers();
  initialize_deffered_actions();
}

void process_timers(void)
{
  major_timer_tick_t *mt,*major_tick=NULL;
  long is;
  struct rb_node *n;
  ulong_t mtickv=system_ticks-(system_ticks % CONFIG_TIMER_GRANULARITY);

  LOCK_SW_TIMERS_R(is);
  n=timers_rb_root.rb_node;

  while( n ) {
    mt=rb_entry(n,major_timer_tick_t,rbnode);

    if( mtickv < mt->time_x ) {
      n=n->rb_left;
    } else if( mtickv > mt->time_x )  {
      n=n->rb_right;
    } else {
      major_tick=mt;
      break;
    }
  }

  UNLOCK_SW_TIMERS_R(is);

  if( major_tick ) { /* Let's see if we have any timers for this major tick. */
    list_head_t *lh=&major_tick->minor_ticks[(system_ticks-mtickv)/MINOR_TICK_GROUP_SIZE];
    list_node_t *ln;
    timer_tick_t *tt;

    LOCK_MAJOR_TIMER_TICK(major_tick,is);
    list_for_each(lh,ln) {
      tt=container_of(ln,timer_tick_t,node);
      if( tt->time_x == system_ticks ) { /* Got something for this tick. */
        ASSERT(tt->major_tick == major_tick);

        __last_processed_timer_tick=system_ticks;
        list_del(&tt->node);
        schedule_deffered_actions(&tt->actions);
        break;
      }
    }
    UNLOCK_MAJOR_TIMER_TICK(major_tick,is);
  }
}


void delete_timer(ktimer_t *timer)
{
  long is;
  timer_tick_t *tt=&timer->minor_tick;

  if( !tt->major_tick ) { /* Ignore clear timers. */
    return;
  }

  LOCK_MAJOR_TIMER_TICK(tt->major_tick,is);
  if( tt->time_x > __last_processed_timer_tick ) {
    /* Timer hasn't triggered yet. So remove it only from timer list.
     */
    if( !list_node_is_bound(&tt->node) ) {
      /* The simpliest case - only one timer in this tick, no rebalance. */
      skiplist_del(&timer->da,deffered_irq_action_t,head,node);
    } else {
      /* Need to rebalance the whole list associated with this timer. */
      ktimer_t *nt;
      list_node_t *ln;

      skiplist_del(&timer->da,deffered_irq_action_t,head,node);
      nt=container_of(list_node_first(&tt->actions),ktimer_t,da);
      list_move2head(&nt->minor_tick.actions,&tt->actions);
      list_replace(&tt->node,&nt->minor_tick.node);
    }
  } else {
    /* Bad luck - timer's action has properly been scheduled. So try
     * to remove it from the list of deferred actions.
     */
    deschedule_deffered_action(&timer->da);
  }
  UNLOCK_MAJOR_TIMER_TICK(tt->major_tick,is);
}

long add_timer(ktimer_t *t)
{
  long is;
  struct rb_node *n;
  ulong_t mtickv;
  long r=0,i;
  major_timer_tick_t *mt;
  struct rb_node ** p;
  list_head_t *lh;
  list_node_t *ln;

  if( !t->time_x || !t->minor_tick.time_x ) {
    return -EINVAL;
  }

  mtickv=t->time_x-(t->time_x % CONFIG_TIMER_GRANULARITY);

  /* First try to locate an existing major tick or create a new one.
   */
  LOCK_SW_TIMERS(is);
  if( t->time_x <= system_ticks  ) {
    r=-EAGAIN;
    goto out;
  }

  n=timers_rb_root.rb_node;
  while( n ) {
    mt=rb_entry(n,major_timer_tick_t,rbnode);

    if( mtickv < mt->time_x ) {
      n=n->rb_left;
    } else if( mtickv > mt->time_x )  {
      n=n->rb_right;
    } else {
      get_major_tick(mt);
      break;
    }
  }

  /* No major tick for target time point, so we should create a new one.
   */
  if( !n ) {
    mt=memalloc(sizeof(*mt));

    if( !mt ) {
      r=-ENOMEM;
      goto out;
    }

    /* Initialize a new entry. */
    atomic_set(&mt->use_counter,1);
    mt->time_x=mtickv;
    spinlock_initialize(&mt->lock);

    for( i=0;i<MINOR_TICK_GROUPS;i++ ) {
      list_init_head(&mt->minor_ticks[i]);
    }

    /* Now insert new tick into RB tree. */
    p=&timers_rb_root.rb_node;
    n=NULL;

    while( *p ) {
      n=*p;
      major_timer_tick_t *_mt=rb_entry(n,major_timer_tick_t,rbnode);

      if( mtickv < _mt->time_x ) {
        p=&(*p)->rb_left;
      } else if( mtickv > _mt->time_x ) {
        p=&(*p)->rb_right;
      }
    }
    rb_link_node(&mt->rbnode,n,p);
    rb_insert_color(&mt->rbnode,&timers_rb_root);
  }
  UNLOCK_SW_TIMERS(is);

  /* OK, our major tick was located so we can add our timer to it.
   */
  t->minor_tick.major_tick=mt;

  LOCK_MAJOR_TIMER_TICK(mt,is);
  if( t->time_x <= system_ticks  ) {
    r=-EAGAIN;
    goto out_unlock_tick;
  }

  lh=&mt->minor_ticks[(t->time_x-mtickv)/MINOR_TICK_GROUP_SIZE];
  if( !list_is_empty(lh) ) {
    list_for_each( lh,ln ) {
      timer_tick_t *tt=container_of(ln,timer_tick_t,node);

      if( tt->time_x == t->time_x ) {
        skiplist_add(&t->da,&tt->actions,deffered_irq_action_t,node,head,priority);
        goto out_insert;
      } else if( tt->time_x > t->time_x ) {
        list_insert_before(&t->minor_tick.node,ln);
        goto out_insert;
      }
      /* Fallthrough in case of the lowest tick value - it will be added to
       * the end of the list.
       */
    }
  }
  /* By default - add this timer to the end of the list. */
  list_add2tail(lh,&t->minor_tick.node);
  list_add2tail(&t->minor_tick.actions,&t->da.node);

out_insert:
  t->minor_tick.major_tick=mt;
  r=0;
out_unlock_tick:
  UNLOCK_MAJOR_TIMER_TICK(mt,is);
  /* TODO: [mt] Cleanup major tick structure upon timer expiration. */
  return r;
out:
  UNLOCK_SW_TIMERS(is);
  return r;
}

static bool __timer_deffered_sched_handler(void *data)
{
  ktimer_t *timer=(ktimer_t *)data;

  return (timer->time_x > system_ticks);
}

long sleep(ulong_t ticks)
{
  ktimer_t timer;
  long r;

  if( !ticks ) {
    return 0;
  }

  init_timer(&timer,system_ticks+ticks,DEF_ACTION_UNBLOCK);
  timer.da.d.target=current_task();

  r=add_timer(&timer);
  if( !r ) {
    sched_change_task_state_deferred(current_task(),TASK_STATE_SLEEPING,
                                     __timer_deffered_sched_handler,&timer);
    
    if( task_was_interrupted(current_task()) ) {
      r=-EINTR;
    }
  } else if( r > 0 ) { /* Expired. */
    r=0;
  }

  return r;
}
