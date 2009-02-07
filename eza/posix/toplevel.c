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
 * posix/toplevel.c: implementation of IRQ deferred actions.
 */

#include <kernel/syscalls.h>
#include <eza/posix.h>
#include <eza/task.h>
#include <kernel/vm.h>
#include <eza/errno.h>
#include <eza/signal.h>
#include <eza/arch/interrupt.h>
#include <eza/arch/profile.h>

long sys_timer_create(clockid_t clockid,struct sigevent *evp,
                      posixid_t *timerid)
{
  task_t *caller=current_task();
  posix_stuff_t *stuff;
  struct sigevent kevp;
  long id,r;
  posix_timer_t *ptimer=NULL;
  siginfo_t *siginfo;

  if( clockid != CLOCK_REALTIME ) {
    return -EINVAL;
  }

  if( evp ) {
    if( copy_from_user(&kevp,evp,sizeof(kevp)) ) {
      return -EFAULT;
    }
    if( !posix_validate_sigevent(&kevp) ) {
      return -EINVAL;
    }
  } else {
    INIT_SIGEVENT(kevp);
  }

  ptimer=memalloc(sizeof(*ptimer));
  if( !ptimer ) {
    return -ENOMEM;
  }

  memset(ptimer,0,sizeof(*ptimer));
  stuff=caller->posix_stuff;

  LOCK_POSIX_STUFF_W(stuff);
  r=-EAGAIN;
  if( ++stuff->timers > CONFIG_POSIX_MAX_TIMERS ) {
    goto out;
  }
  id=posix_allocate_obj_id(stuff);
  if( id < 0 ) {
    goto out;
  }
  UNLOCK_POSIX_STUFF_W(stuff);

  if( !evp ) {
    kevp.sigev_value.sival_int=id;
  }

  POSIX_KOBJ_INIT(&ptimer->kpo,POSIX_OBJ_TIMER,id);
  init_timer(&ptimer->ktimer,0,DEF_ACTION_SIGACTION);
  ptimer->ktimer.da.kern_priv=ptimer;
  ptimer->overrun=0;

  switch( kevp.sigev_notify ) {
    case SIGEV_SIGNAL:
      siginfo=&ptimer->ktimer.da.d.siginfo;
      INIT_SIGINFO_CURR(siginfo);
      siginfo->si_signo=kevp.sigev_signo;
      siginfo->si_value=kevp.sigev_value;
      break;
  }

  if( copy_to_user(timerid,&id,sizeof(id)) ) {
    r=-EFAULT;
    goto free_id;
  }

  LOCK_POSIX_STUFF_W(stuff);
  posix_insert_object(stuff,&ptimer->kpo,id);
  stuff->timers++;
  UNLOCK_POSIX_STUFF_W(stuff);

  return 0;
free_id:
  LOCK_POSIX_STUFF_W(stuff);
  posix_free_obj_id(stuff,id);
out:
  stuff->timers--;
  UNLOCK_POSIX_STUFF_W(stuff);

  if( ptimer ) {
    memfree(ptimer);
  }
  return r;
}

static void __get_timer_status(posix_timer_t *ptimer,itimerspec_t *kspec)
{
  ktimer_t *timer=&ptimer->ktimer;

  ticks_to_time(&kspec->it_interval,ptimer->interval);

  /* We disable interrupts to prevent us from being preempted, and,
   * therefore, from calculating wrong time delta.
   */
  interrupts_disable();
  if( timer->time_x > system_ticks ) {
    ticks_to_time(&kspec->it_value,timer->time_x - system_ticks);
  } else {
    kspec->it_value.tv_sec=kspec->it_value.tv_nsec=0;
  }
  interrupts_enable();
}

long sys_timer_control(long id,long cmd,long arg1,long arg2,long arg3)
{
  long r=-EINVAL;
  task_t *caller=current_task();
  posix_stuff_t *stuff=caller->posix_stuff;
  posix_timer_t *ptimer;
  itimerspec_t tspec,kspec;
  ktimer_t *ktimer;

  switch( cmd ) {
    case __POSIX_TIMER_SETTIME:
      /* Arguments are the same as for POSIX 'timer_settime()':
       *    arg1: int flags, arg2: struct itimerspec *value
       *    arg3: struct itimerspec *ovalue
       */
      if( !arg2 || copy_from_user(&tspec,(void *)arg2,sizeof(tspec)) ) {
        r=-EFAULT;
      } else {
        bool valid_timeval=timeval_is_valid(&tspec.it_value) && timeval_is_valid(&tspec.it_interval);
        ulong_t tx=time_to_ticks(&tspec.it_value);
        ulong_t itx=time_to_ticks(&tspec.it_interval);
        ulong_t t1,t2;

        /* We need to hold the lock during the whole process, so lookup
         * target timer explicitely.
         */
        LOCK_POSIX_STUFF_W(stuff);
        __READ_TIMESTAMP_COUNTER(t1);
        ptimer=(posix_timer_t*)__posix_locate_object(stuff,id,POSIX_OBJ_TIMER);
        if( !ptimer ) {
          UNLOCK_POSIX_STUFF_W(stuff);
          break;
        }

        ktimer=&ptimer->ktimer;
        if( !(tspec.it_value.tv_sec | tspec.it_value.tv_nsec) ) {
          if( ktimer->time_x ) {
            /* Disarm real timer  */
          }
        } else if( valid_timeval ) {
          if( !(arg1 & TIMER_ABSTIME) ) {
            tx+=system_ticks;
          }

          ptimer->interval=itx;
          TIMER_RESET_TIME(ktimer,tx);
          r=add_timer(ktimer);
        }
        __READ_TIMESTAMP_COUNTER(t2);
        UNLOCK_POSIX_STUFF_W(stuff);

        if( !r && arg3 ) {
          __get_timer_status(ptimer,&kspec);
          if( copy_to_user((itimerspec_t *)arg3,&kspec,sizeof(kspec)) )  {
            r=-EFAULT;
            /* TODO: [mt] Cleanup timer upon -EFAULT. */
          }
        }
      }
      break;
    case __POSIX_TIMER_GETTIME:
      ptimer=posix_lookup_timer(stuff,id);
      if( !ptimer ) {
        break;
      }
      __get_timer_status(ptimer,&kspec);
      r=copy_to_user((itimerspec_t *)arg1,&kspec,sizeof(kspec)) ? -EFAULT : 0;
      break;
    case __POSIX_TIMER_GETOVERRUN:
      ptimer=posix_lookup_timer(stuff,id);
      if( !ptimer ) {
        break;
      }
      r=ptimer->overrun;
      break;
    default:
      r=-EINVAL;
      break;
  }

  if( ptimer ) {
    release_posix_timer(ptimer);
  }
  return r;
}
