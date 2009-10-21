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
 * (c) Copyright 2006,2007,2008,2009 MString Core Team <http://mstring.berlios.de>
 * (c) Copyright 2009 Michael Tsymbalyuk <mtzaurus@gmail.com>
 *
 * kernel/security/toplevel.c: Syscalls related to the kernel security facility.
 */

#include <mstring/types.h>
#include <security/security.h>
#include <sync/spinlock.h>
#include <mstring/task.h>
#include <arch/current.h>
#include <security/util.h>
#include <mm/slab.h>
#include <arch/atomic.h>
#include <mstring/process.h>
#include <mstring/usercopy.h>

long sys_mac_control(ulong_t cmd, ulong_t arg, void *data)
{
  long r=-EPERM;
  task_t *target=NULL;
  struct __s_object *csobject=S_GET_INVOKER();
  struct __s_object *tobject;
  struct __mac_ctl_arg ctl_arg;

  switch( cmd ) {
    case S_MAC_CTL_SET_LABEL:
      if( copy_from_user(&ctl_arg,data,sizeof(ctl_arg)) ) {
        r=-EFAULT;
      } else {
        if( s_check_system_capability(SYS_CAP_ADMIN) ) {
          if( !(target=pid_to_task(arg)) ) {
            r=-ESRCH;
          } else {
            tobject=S_GET_TASK_OBJ(target);
            if( s_check_access(csobject,tobject) ) {
              S_LOCK_OBJECT_W(tobject);
              S_UNLOCK_OBJECT_W(tobject);
            }
          }
        }
      }
      break;
    default:
      r=-EINVAL;
      break;
  }

  if( target ) {
    release_task_struct(target);
  }

  return ERR(r);
}
