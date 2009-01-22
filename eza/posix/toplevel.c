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
#include <eza/errno.h>
#include <eza/usercopy.h>
#include <eza/signal.h>

int sys_timer_create(clockid_t clockid,struct sigevent *evp,
                          posixid_t *timerid)
{
  task_t *caller=current_task();
  posix_stuff_t *stuff;
  struct sigevent kevp;
  long id;
  int r;
  posix_timer_t *ptimer;

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

  stuff=get_task_posix_stuff(caller);
  if( !stuff ) {
    return -EINVAL;
  }

  LOCK_POSIX_STUFF_W(stuff);
  id=posix_allocate_obj_id(stuff);
  if( id < 0 ) {
    r=-EAGAIN;
    goto out;
  }
  UNLOCK_POSIX_STUFF_W(stuff);

  ptimer=memalloc(sizeof(*ptimer));
  if( !ptimer ) {
    r=-ENOMEM;
    goto free_id;
  }

  if( !evp ) {
    kevp.sigev_value.sival_int=id;
  }

  POSIX_KOBJ_INIT(&ptimer->kpo,POSIX_OBJ_TIMER);
  ptimer->sevent=kevp;

  if( copy_to_user(timerid,&id,sizeof(id)) ) {
    r=-EFAULT;
    goto free_id;
  }

  LOCK_POSIX_STUFF_W(stuff);
  posix_insert_object(stuff,&ptimer->kpo,id);
  UNLOCK_POSIX_STUFF_W(stuff);

  release_task_posix_stuff(stuff);
  return 0;
free_id:
  LOCK_POSIX_STUFF_W(stuff);
  posix_free_obj_id(stuff,id);
out:
  UNLOCK_POSIX_STUFF_W(stuff);
  release_task_posix_stuff(stuff);
  return r;
}
