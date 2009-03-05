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
#include <eza/kconsole.h>
#include <eza/serial.h>

/*spinlock*/
static SPINLOCK_DEFINE(timer_lock);
static SPINLOCK_DEFINE(sw_timers_lock);

/*list of the timers*/
static LIST_DEFINE(known_hw_timers);
static LIST_DEFINE(expired_major_ticks);
static LIST_DEFINE(cached_major_ticks);
static int __num_cached_major_ticks;

#ifdef CONFIG_TIMER_RBTREE
static struct rb_root timers_rb_root;
#else
static LIST_DEFINE(timers_list);
#endif

static ulong_t __last_processed_timer_tick;

#define GRAB_HW_TIMER_LOCK() spinlock_lock(&timer_lock)
#define RELEASE_HW_TIMER_LOCK() spinlock_unlock(&timer_lock)

#define LOCK_SW_TIMERS(l)  spinlock_lock_irqsave(&sw_timers_lock,l)
#define UNLOCK_SW_TIMERS(l) spinlock_unlock_irqrestore(&sw_timers_lock,l)

#define LOCK_SW_TIMERS_R(l)  spinlock_lock_irqsave(&sw_timers_lock,l)
#define UNLOCK_SW_TIMERS_R(l) spinlock_unlock_irqrestore(&sw_timers_lock,l)

#define get_major_tick(t) atomic_inc(&(t)->use_counter)

#define GET_NEW_MAJOR_TICK(_pm,_cu,_mtickv) do {                \
    if( !list_is_empty(&cached_major_ticks) ) {                 \
      (_pm)=container_of(list_node_first(&cached_major_ticks),  \
                         major_timer_tick_t,list);              \
      list_del(&(_pm)->list);                                   \
      MAJOR_TIMER_TICK_INIT((_pm),(_mtickv));                   \
      (_cu)=true;                                               \
      __num_cached_major_ticks--;                               \
    } else {                                                    \
      (_cu)=false;                                              \
      (_pm)=memalloc(sizeof(major_timer_tick_t));               \
      if( (_pm) ) {                                             \
        MAJOR_TIMER_TICK_INIT((_pm),(_mtickv));                 \
      }                                                         \
    }                                                           \
  } while(0)


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

static void __init_sw_timers(void)
{
  int i;

  /*Paranoya check to prevent improper kernel configuration. */
  ASSERT(CONFIG_MIN_CACHED_MAJOR_TICKS<=CONFIG_CACHED_MAJOR_TICKS);

  __num_cached_major_ticks=CONFIG_CACHED_MAJOR_TICKS;
  for(i=0;i<CONFIG_CACHED_MAJOR_TICKS;i++) {
    major_timer_tick_t *mt=memalloc(sizeof(*mt));

    if( !mt ) {
      panic("Can't allocate memory for pre-cached timer ticks !");
    }
    MAJOR_TIMER_TICK_INIT(mt,0);
    list_add2tail(&cached_major_ticks,&mt->list);
  }
}

void init_timers(void)
{
  init_hw_timers();
  __init_sw_timers();
  initialize_deffered_actions();
}

static void __dump_major_tick(major_timer_tick_t *mt)
{
  kconsole_t *cons=get_fault_console();
  char buf[256];
  int i;

  sprintf(buf,"***** DUMPING MAJOR TICK %d\n",mt->time_x);
  cons->display_string(buf);

  for(i=0;i<MINOR_TICK_GROUPS;i++) {
    if( !list_is_empty(&mt->minor_ticks[i]) ) {
      list_node_t *ln,*ln_a,*ln_b;
      deffered_irq_action_t *da;
      ktimer_t *kt;

      list_for_each(&mt->minor_ticks[i],ln) {
        timer_tick_t *tt=container_of(ln,timer_tick_t,node);

        sprintf(buf,"TIMER TICK: %d\n",tt->time_x);

        list_for_each(&tt->actions,ln_a) {
          da=container_of(ln_a,deffered_irq_action_t,node);
          kt=container_of(da,ktimer_t,da);

          sprintf(buf,"    <%d:%d>\n",kt->time_x,da->priority);
          cons->display_string(buf);

          if( !list_is_empty(&da->head) ) {
            list_for_each(&da->head,ln_b) {
              da=container_of(ln_b,deffered_irq_action_t,node);
              kt=container_of(da,ktimer_t,da);

              sprintf(buf,"                <%d:%d>\n",kt->time_x,da->priority);
              cons->display_string(buf);
            }
          }
        }
      }
    }
  }
  sprintf(buf,"***** FINISHED DUMPING MAJOR TICK %d\n",mt->time_x);
  cons->display_string(buf);
}

void process_timers(void)
{
#ifdef CONFIG_TIMER_RBTREE
  struct rb_node *n;
#endif
  major_timer_tick_t *mt,*major_tick=NULL;
  long is;
  ulong_t mtickv=system_ticks-(system_ticks % CONFIG_TIMER_GRANULARITY);

  LOCK_SW_TIMERS_R(is);
#ifdef CONFIG_TIMER_RBTREE
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
#else
  if( !list_is_empty(&timers_list) ) {
    mt=container_of(list_node_first(&timers_list),major_timer_tick_t,list);
    if( mt->time_x <= mtickv ) {
      major_tick=mt;
    }
  }
#endif
  UNLOCK_SW_TIMERS_R(is);

  if( major_tick ) { /* Let's see if we have any timers for this major tick. */
    list_head_t *lh=&major_tick->minor_ticks[(system_ticks-mtickv)/MINOR_TICK_GROUP_SIZE];
    list_node_t *ln;
    timer_tick_t *tt;
    bool expired=(system_ticks==major_tick->time_x+CONFIG_TIMER_GRANULARITY-1);

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

    if( expired ) {
      LOCK_SW_TIMERS_R(is);
#ifndef CONFIG_TIMER_RBTREE
      list_del(&major_tick->list);
      list_add2tail(&cached_major_ticks,&major_tick->list);
      __num_cached_major_ticks++;
#else
      list_add2tail(&expired_major_ticks,&major_tick->list);
#endif
      UNLOCK_SW_TIMERS_R(is);
    }
  }
}


void delete_timer(ktimer_t *timer)
{
  long is;
  timer_tick_t *tt=&timer->minor_tick;
  major_timer_tick_t *mtt;

  if( !tt->major_tick ) { /* Ignore clear timers. */
    return;
  }

  mtt=tt->major_tick;
  LOCK_MAJOR_TIMER_TICK(mtt,is);
  atomic_dec(&mtt->use_counter);

  if( tt->time_x > __last_processed_timer_tick ) {
    /* Timer hasn't triggered yet. So remove it only from timer list.
     */
    if( !list_node_is_bound(&tt->node) ) {
      /* The simpliest case - only one timer in this tick, no rebalance. */
      skiplist_del(&timer->da,deffered_irq_action_t,head,node);
    } else {
      /* Need to rebalance the whole list associated with this timer. */
      ktimer_t *nt;
      list_node_t *lhn=&tt->actions.head;

      if( lhn->next != &timer->da.node || lhn->prev != &timer->da.node
          || !list_is_empty(&timer->da.head) ) {

        if( !list_is_empty(&timer->da.head) ) {
          deffered_irq_action_t *da=container_of(list_node_first(&timer->da.head),
                                                 deffered_irq_action_t,node);
          list_del(&da->node);
          if( !list_is_empty(&timer->da.head) ) {
            list_move2head(&da->head,&timer->da.head);
          }
          list_replace(&timer->da.node,&da->node);
        } else {
          list_del(&timer->da.node);
        }

        nt=container_of(list_node_first(&tt->actions),ktimer_t,da);
        nt->minor_tick.actions.head.next=&nt->da.node;
        nt->da.node.prev=&nt->minor_tick.actions.head;
        nt->minor_tick.actions.head.prev=tt->actions.head.prev;
        tt->actions.head.prev->next=&nt->minor_tick.actions.head;
        list_replace(&tt->node,&nt->minor_tick.node);
      } else {
        list_del(&tt->node);
      }
    }
  } else {
    /* Bad luck - timer's action has properly been scheduled. So try
     * to remove it from the list of deferred actions.
     */
    deschedule_deffered_action(&timer->da);
  }
  UNLOCK_MAJOR_TIMER_TICK(mtt,is);

  if( !atomic_get(&mtt->use_counter) ) {
  }
}

long modify_timer(ktimer_t *timer,ulong_t time_x)
{
  long r;
  major_timer_tick_t *mt;

  if( time_x <= system_ticks ) {
    execute_deffered_action(&timer->da);
    return 0;
  }

  mt=timer->minor_tick.major_tick;
  if( !mt ) { /* Ignore clear timers. */
    return -EINVAL;
  }

  delete_timer(timer);
  TIMER_RESET_TIME(timer,time_x);
  r=add_timer(timer);

  return r;
}

long add_timer(ktimer_t *t)
{
#ifdef CONFIG_TIMER_RBTREE
  struct rb_node *n;
  struct rb_node **p;
#else
  list_node_t *succ;
#endif
  long is;
  ulong_t mtickv;
  long r=0;
  major_timer_tick_t *mt=NULL,*_mt;
  list_head_t *lh;
  list_node_t *ln;
  bool cache_used=false;

  if( !t->time_x || !t->minor_tick.time_x ) {
    return -EINVAL;
  }

  mtickv=t->time_x-(t->time_x % CONFIG_TIMER_GRANULARITY);

  LOCK_SW_TIMERS(is);
  if( t->time_x <= system_ticks  ) {
    r=-EAGAIN;
    goto out;
  }

#ifdef CONFIG_TIMER_RBTREE
  p=&timers_rb_root.rb_node;
  n=NULL;

  while( *p ) {
    n=*p;

    _mt=rb_entry(n,major_timer_tick_t,rbnode);
    if( _mt->time_x == mtickv ) {
      mt=_mt;
      get_major_tick(mt);
      break;
    }

    if( mtickv < _mt->time_x ) {
      p=&(*p)->rb_left;
    } else if( mtickv > _mt->time_x ) {
      p=&(*p)->rb_right;
    }
  }

  if( !mt ) { /* No major tick found, so create a new one. */
    GET_NEW_MAJOR_TICK(mt,cache_used,mtickv);
    if( !mt ) {
      r=-ENOMEM;
      goto out;
    }

    rb_link_node(&mt->rbnode,n,p);
    rb_insert_color(&mt->rbnode,&timers_rb_root);
  }
#else /* List-based timer representation. */
  succ=NULL;
  list_for_each(&timers_list,ln) {
    _mt=container_of(ln,major_timer_tick_t,list);

    if( _mt->time_x == mtickv ) {
      mt=_mt;
      get_major_tick(mt);
      break;
    } else if( _mt->time_x > mtickv ) {
      succ=ln;
      break;
    }
  }

  if( !mt ) {
    GET_NEW_MAJOR_TICK(mt,cache_used,mtickv);
    if( !mt ) {
      r=-ENOMEM;
      goto out;
    }

    if( succ ) {
      list_insert_before(&mt->list,succ);
    } else {
      list_add2tail(&timers_list,&mt->list);
    }
  }
#endif
  UNLOCK_SW_TIMERS(is);

  /* OK, our major tick was located so we can add our timer to it.
   */
  t->minor_tick.major_tick=mt;

  LOCK_MAJOR_TIMER_TICK(mt,is);
  if( t->time_x <= __last_processed_timer_tick  ) {
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
        list_add2tail(&t->minor_tick.actions,&t->da.node);
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
fire_expired_timer:
  if( r == -EAGAIN ) {
    execute_deffered_action(&t->da);
  }

  /* Now check if we need to remove any expired major ticks from
   * the RB-tree and put them in the list of cached major ticks.
   */
#ifdef CONFIG_TIMER_RBTREE
  LOCK_SW_TIMERS(is);
  if( !list_is_empty(&expired_major_ticks) ) {
    mt=container_of(list_node_first(&expired_major_ticks),
                    major_timer_tick_t,list);
    list_del(&mt->list);
    rb_erase(&mt->rbnode,&timers_rb_root);
    list_add2tail(&cached_major_ticks,&mt->list);
    __num_cached_major_ticks++;
    cache_used=false;
  }
  UNLOCK_SW_TIMERS(is);
#endif

  if( cache_used && __num_cached_major_ticks < CONFIG_MIN_CACHED_MAJOR_TICKS ) {
    mt=memalloc(sizeof(*mt));
    if( mt ) {
      MAJOR_TIMER_TICK_INIT(mt,0);
      LOCK_SW_TIMERS(is);
      list_add2tail(&cached_major_ticks,&mt->list);
      __num_cached_major_ticks++;
      UNLOCK_SW_TIMERS(is);
    }
  }

  return r;
out:
  UNLOCK_SW_TIMERS(is);
  goto fire_expired_timer;
}

static bool __timer_deffered_sched_handler(void *data)
{
  ktimer_t *timer=(ktimer_t *)data;
  return (timer->time_x > system_ticks);
}

#ifdef CONFIG_TIMER_RBTREE
void __dump_timers(long tick)
{
  major_timer_tick_t *mt,*major_tick=NULL;
  long is;
  struct rb_node *n;
  ulong_t mtickv=tick-(tick % CONFIG_TIMER_GRANULARITY);

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

  if( major_tick ) {
    int i;

    spinlock_lock(&major_tick->lock);
    kprintf("[%d] <%d>: ",cpu_id(),major_tick->time_x);
    for(i=0;i<MINOR_TICK_GROUPS;i++) {
      list_head_t *lh=&major_tick->minor_ticks[i];
      list_node_t *ln;
      timer_tick_t *tt;

      list_for_each(lh,ln) {
        tt=container_of(ln,timer_tick_t,node);
        ASSERT(tt->major_tick == major_tick);
        kprintf(" (%d) ",tt->time_x);
      }
      kprintf("\n");
    }
    spinlock_unlock(&major_tick->lock);
  }
  UNLOCK_SW_TIMERS_R(is);
}
#endif

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
    r=sched_change_task_state_deferred(current_task(),TASK_STATE_SLEEPING,
                                       __timer_deffered_sched_handler,&timer);
    if( task_was_interrupted(current_task()) ) {
      r=-EINTR;
    }
  } else if( r > 0 ) { /* Expired. */
    r=0;
  }

  delete_timer(&timer);
  return r;
}
