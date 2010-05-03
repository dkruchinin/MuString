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
#include <mstring/assert.h>

struct __s_op_param {
  struct {
    struct __mac_ctl_arg in_data,out_data;
  } d;
  ulong_t arg;
  struct {
    task_t *task;
    ipc_gen_port_t *port;
  } target;
};

typedef long (*__s_op_handler_t)(struct __s_op_param *param);

/* Flags for defining common logic in operation descriptors. */
#define _PID_AS_ARG    0x1
#define _IN_DATA       0x2
#define _OUT_DATA      0x4
#define _ADMIN_OP      0x8
#define _IPC_OBJ       0x10
#define _ONLY_WEAKENING 0x20 /* Allow only weakening MAC label */

struct __s_op_descr {
  __s_op_handler_t handler;
  long flags;
  long cmd;
};

static long set_task_label_handler(struct __s_op_param *param)
{
  return s_set_obj_cred(S_GET_TASK_OBJ(param->target.task),
                        S_CRED_MAC_LABEL,&param->d.in_data.creds);
}

static long get_task_creds_handler(struct __s_op_param *param)
{
  return s_get_obj_cred(S_GET_TASK_OBJ(param->target.task),
                        S_CRED_WHOLE,&param->d.out_data.creds);
}

static long set_task_uid_handler(struct __s_op_param *param)
{
  return s_set_obj_cred(S_GET_TASK_OBJ(param->target.task),
                        S_CRED_UID,&param->d.in_data.creds);
}

static long set_sys_cap_handler(struct __s_op_param *param)
{
  return s_set_sys_mac_label(param->arg,&param->d.in_data.creds.mac_label);
}

static long get_sys_cap_handler(struct __s_op_param *param)
{
  return s_get_sys_mac_label(param->arg,&param->d.out_data.creds.mac_label);
}

static long set_ipc_port_label(struct __s_op_param *param)
{
  return s_set_obj_cred(S_GET_PORT_OBJ(param->target.port),
                        S_CRED_MAC_LABEL,&param->d.in_data.creds);
}

static long get_ipc_port_label(struct __s_op_param *param)
{
  return s_get_obj_cred(S_GET_PORT_OBJ(param->target.port),
                        S_CRED_MAC_LABEL,&param->d.out_data.creds);
}

static struct __s_op_descr s_op_descrs[]={
  {set_task_label_handler, _PID_AS_ARG|_IN_DATA|_ONLY_WEAKENING, S_MAC_CTL_SET_LABEL},
  {get_task_creds_handler, _PID_AS_ARG|_OUT_DATA, S_MAC_CTL_GET_CREDS},
  {set_task_uid_handler, _PID_AS_ARG|_IN_DATA|_ADMIN_OP, S_MAC_CTL_SET_UID},
  {get_sys_cap_handler, _OUT_DATA, S_MAC_CTL_GET_SYS_CAP},
  {set_sys_cap_handler,_IN_DATA|_ADMIN_OP,S_MAC_CTL_SET_SYS_CAP},
  {set_ipc_port_label, _IN_DATA|_OUT_DATA|_ONLY_WEAKENING|_IPC_OBJ,S_MAC_CTL_SET_PORT_LABEL},
  {get_ipc_port_label, _OUT_DATA|_IN_DATA|_IPC_OBJ,S_MAC_CTL_GET_PORT_LABEL},
};

static void __release_targets(struct __s_op_param *op_p)
{
  if( op_p->target.task ) {
    release_task_struct(op_p->target.task);
  } else if( op_p->target.port ) {
    ipc_put_port(op_p->target.port);
  }
  memset(&op_p->target,0,sizeof(op_p->target));
}

/* NOTE: must be called _after_ transferring input data from userland ! */
static long __decode_targets(ulong_t cmd, ulong_t arg,
                             struct __s_op_descr *op_descr,struct __s_op_param *op_p)
{
  struct __s_object *target=NULL;
  long r=0;
  task_t *t;
  bool weakening = (op_descr->flags & _ONLY_WEAKENING) && !s_check_system_capability(SYS_CAP_ADMIN);
  task_t *caller = current_task();

  memset(&op_p->target,0,sizeof(op_p->target));
  op_p->arg=arg;

  if( op_descr->flags & _PID_AS_ARG ) { /* PID ? */
    if (arg) {
      if (weakening) {
        if (arg != caller->pid) {
          r=-EPERM;
        }
      }
      if (!r && !(t=pid_to_task(arg)) ) {
        r=-ESRCH;
      }
    } else {
      t = caller;
      grab_task_struct(t);
    }

    if (t) {
      op_p->target.task = t;
      target=S_GET_TASK_OBJ(t);
    }
  } else if( op_descr->flags & _IPC_OBJ ) { /* IPC port ? */
    if( arg ) {
      /* Weakening targets only callers itselves. */
      if (weakening && arg != current_task()->pid) {
        r=-EPERM;
      }

      if( !r && !(t=pid_to_task(arg)) ) {
        r=-ESRCH;
      }
    } else {
      t=current_task();
      grab_task_struct(t);
    }

    if( t ) {
      ipc_gen_port_t *port = ipc_get_port(t,op_p->d.in_data.target_obj.port,&r);
      release_task_struct(t);

      if (!port) {
        r=-EINVAL;
      } else {
        op_p->target.port = port;
        target=S_GET_PORT_OBJ(port);
      }
    }
  }

  if (target) {
    if (s_check_access(S_GET_INVOKER(),target)) {
      if (weakening) {
        /* Only MAC weakening is acceptible. */
        if (!S_MACS_WE(op_p->d.in_data.creds.mac_label,target->creds.mac_label)) {
          r=-EPERM;
        }
      }
    } else {
      r=-EPERM;
    }

    if (r) {
      __release_targets(op_p);
    }
  }

  return ERR(r);
}

long sys_security_control(ulong_t cmd, ulong_t arg, void *data)
{
  long r=0;
  struct __s_op_param op_p;
  struct __s_op_descr *op_descr;

  if( cmd > S_MAX_CTL_LAST_CMD ) {
    return ERR(-EINVAL);
  }

  op_descr=&s_op_descrs[cmd];
  ASSERT(cmd==op_descr->cmd);

  if( (op_descr->flags & _ADMIN_OP) &&
      !s_check_system_capability(SYS_CAP_ADMIN)) {
    return ERR(-EPERM);
  }

  if( op_descr->flags & _IN_DATA ) {
    if( copy_from_user(&op_p.d.in_data,data,sizeof(op_p.d.in_data)) ) {
      return ERR(-EFAULT);
    }
  }

  if( !(r=__decode_targets(cmd,arg,op_descr,&op_p)) ) {
    if( op_descr->flags & _OUT_DATA) { /* Prepare output buffer. */
      memset(&op_p.d.out_data,0,sizeof(op_p.d.out_data));
    }

    r=op_descr->handler(&op_p);

    if( !r && (op_descr->flags & _OUT_DATA) ) {
      r=copy_to_user(data,&op_p.d.out_data,sizeof(op_p.d.out_data)) ? -EFAULT : 0;
    }

    __release_targets(&op_p);
  }

  return ERR(r);
}

/* These obsolete functions will be removed after migrating to
 * the new security API.
 */
long set_task_creds(task_t *target, void *ubuffer)
{
  uidgid_t uidgid;
  struct __s_object *tobject=S_GET_TASK_OBJ(target);

  if( !s_check_system_capability(SYS_CAP_ADMIN)
      || !s_check_access(S_GET_INVOKER(),S_GET_TASK_OBJ(target)) ) {
    return ERR(-EPERM);
  }
  if( copy_from_user(&uidgid,ubuffer,sizeof(uidgid)) ) {
    return ERR(-EFAULT);
  }

  S_LOCK_OBJECT_W(tobject);
  tobject->creds.uid=uidgid.uid;
  tobject->creds.gid=uidgid.gid;
  S_UNLOCK_OBJECT_W(tobject);

  return 0;
}

long get_task_creds(task_t *target, void *ubuffer)
{
  uidgid_t uidgid;
  struct __s_object *tobject=S_GET_TASK_OBJ(target);

  if( !s_check_access(S_GET_INVOKER(),S_GET_TASK_OBJ(target)) ) {
    return ERR(-EPERM);
  }

  S_LOCK_OBJECT_W(tobject);
  uidgid.uid=tobject->creds.uid;
  uidgid.gid=tobject->creds.gid;
  S_UNLOCK_OBJECT_W(tobject);

  if( copy_to_user(ubuffer,&uidgid,sizeof(uidgid)) ) {
    return ERR(-EFAULT);
  }
  return 0;
}
