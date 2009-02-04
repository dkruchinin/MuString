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
 * posix/toplevel.c: implementation of IRQ deferred actions.
 */

#include <kernel/syscalls.h>
#include <eza/posix.h>
#include <eza/task.h>
#include <kernel/vm.h>
#include <eza/errno.h>
#include <eza/signal.h>

long sys_timer_create(clockid_t clockid,struct sigevent *evp,
                      posixid_t *timerid)
{
  task_t *caller=current_task();
  posix_stuff_t *stuff;
  struct sigevent kevp;
  long id,r;
  posix_timer_t *ptimer=NULL;

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

  stuff=get_task_posix_stuff(caller);

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

  switch( kevp.sigev_notify ) {
    case SIGEV_SIGNAL:
      ptimer->ktimer.da.d.pid=caller->pid;
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

  release_task_posix_stuff(stuff);
  return 0;
free_id:
  LOCK_POSIX_STUFF_W(stuff);
  posix_free_obj_id(stuff,id);
out:
  stuff->timers--;
  UNLOCK_POSIX_STUFF_W(stuff);
  release_task_posix_stuff(stuff);

  if( ptimer ) {
    memfree(ptimer);
  }
  return r;
}

long sys_timer_control(long id,long cmd,long arg1,long arg2,long arg3)
{
  long r=0;
  task_t *caller=current_task();
  posix_stuff_t *stuff=get_task_posix_stuff(caller);
  posix_timer_t *ptimer=posix_lookup_timer(stuff,id);

  if( !ptimer ) {
    r=-EINVAL;
    goto put_stuff;
  }
  
  kprintf("[!!!] Timer %d located ! %p, CMD=%d\n",
          id,ptimer,cmd);

  switch( cmd ) {
    case __POSIX_TIMER_SETTIME:
      break;
    case __POSIX_TIMER_GETTIME:
      break;
    case __POSIX_TIMER_GETOVERRUN:
      break;
    default:
      r=-EINVAL;
      break;
  }

  release_posix_timer(ptimer);
put_stuff:
  release_task_posix_stuff(stuff);
  return r;
}
