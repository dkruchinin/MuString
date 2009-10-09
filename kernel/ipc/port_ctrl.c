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
 * (c) Copyright 2009 Michael Tsymbalyuk <mtzaurus@gmail.com>
 *
 * ipc/port_ctrl.c: implementation of IPC ports control logic.
 *
 */

#include <mstring/types.h>
#include <ipc/port.h>
#include <mstring/task.h>
#include <mstring/errno.h>
#include <sync/mutex.h>
#include <mm/page_alloc.h>
#include <mm/page.h>
#include <ds/idx_allocator.h>
#include <ipc/ipc.h>
#include <ipc/buffer.h>
#include <mstring/stddef.h>
#include <arch/preempt.h>
#include <mstring/kconsole.h>
#include <mm/slab.h>
#include <ipc/port.h>
#include <mstring/event.h>
#include <mstring/signal.h>
#include <mstring/usercopy.h>
#include <ipc/port_ctl.h>

static long __port_ctl_forward_message(ipc_gen_port_t *port,
                                       ipc_port_ctl_msg_ch_descr_t *uarg)
{
  ipc_port_ctl_msg_ch_descr_t d;
  ipc_channel_t *c;
  ipc_gen_port_t *dest_port;
  long r;

  if( copy_from_user( &d, uarg, sizeof(d) ) ) {
    return ERR(-EFAULT);
  }

  c = ipc_get_channel(current_task(), d.ch_id);
  if( !c ) {
    return ERR(-EINVAL);
  }

  /* To avoid deadlocks we don't allow forwarding to the same port. */
  dest_port = c->server_port;
  if( dest_port == port ) {
    return ERR(-EINVAL);
  }

  ipc_lock_ports(port,dest_port);
  if( (port->flags & IPC_PORT_SHUTDOWN) || (dest_port->flags & IPC_PORT_SHUTDOWN) ) {
    r=-EINVAL;
  } else {
    ipc_port_message_t *msg = __ipc_port_lookup_message(port,d.msg_id,&r);

    if( msg ) {
      if( msg->state == MSG_STATE_REPLY_BEGIN ||
          msg->state == MSG_STATE_DATA_UNDER_ACCESS ) {
        r=-EINVAL;
      } else {
        port->msg_ops->remove_message(port,msg);
        dest_port->msg_ops->insert_message(dest_port,msg);

        IPC_UNLOCK_PORT_W(port);
        waitqueue_pop(&dest_port->waitqueue,NULL);
        r=0;
        goto unlock_dest;
      }
    }
  }
  IPC_UNLOCK_PORT_W(port);
unlock_dest:
  IPC_UNLOCK_PORT_W(dest_port);

  return ERR(r);
}

static long __port_ctl_append_to_message(ipc_gen_port_t *p,
                                         port_ctl_msg_extra_data_t *ugap)
{
  ipc_port_message_t *msg;
  long r=-EINVAL;
  port_ctl_msg_extra_data_t kgap;

  if( copy_from_user(&kgap, ugap, sizeof(kgap)) ) {
    return ERR(-EFAULT);
  }

  if( !valid_iovecs(&kgap.d.data,1,NULL) ||
      !(kgap.flags & IPC_PORT_CTL_DIR_HEAD)) {
    return ERR(-EINVAL);
  }

  if( !kgap.d.data.iov_len ) {
    return 0;
  }

  IPC_LOCK_PORT_W(p);
  if( !(p->flags & IPC_PORT_SHUTDOWN) &&
      (msg = __ipc_port_lookup_message(p,kgap.msg_id,&r)) && message_writable(msg) ) {
    mark_message_waccess(msg);
    r=0;
  }
  IPC_UNLOCK_PORT_W(p);

  if( r ) {
    return ERR(r);
  }

  if( !msg->extra_data ) {
    msg->extra_data = alloc_pages_addr(IPC_MSG_EXTRA_PAGES,0);
    if( !msg->extra_data ) {
      r=-ENOMEM;
    } else {
      msg->extra_data_tail=IPC_MSG_EXTRA_SIZE;
    }
  }

  if( !r ) {
    /* Make sure we fit into extra buffer. */
    if( kgap.d.data.iov_len > (IPC_MSG_EXTRA_SIZE-(IPC_MSG_EXTRA_SIZE - msg->extra_data_tail)) ) {
      r=-EINVAL;
    } else {
      char *eptr;
      int to_copy;

      msg->extra_data_tail -= kgap.d.data.iov_len;
      eptr = (char *)msg->extra_data + msg->extra_data_tail;
      to_copy = IPC_MSG_EXTRA_SIZE - msg->extra_data_tail;

      if( copy_from_user(eptr, kgap.d.data.iov_base,kgap.d.data.iov_len ) ) {
        r=-EFAULT;
      }
    }
  }

  IPC_LOCK_PORT_W(p);
  if( r < 0 ) {
    /* Adjust extra data pointer in case of errors. */
    msg->extra_data_tail += kgap.d.data.iov_len;
  } else {
    msg->data_size += r;
  }
  unmark_message_waccess(msg);
  IPC_UNLOCK_PORT_W(p);

  return ERR(r);
}

static long __port_ctl_cut_from_message(ipc_gen_port_t *p,
                                        port_ctl_msg_extra_data_t *ugap)
{
  ipc_port_message_t *msg;
  long r=-EINVAL;
  port_ctl_msg_extra_data_t kgap;
  size_t to_cut;

  if( copy_from_user(&kgap, ugap, sizeof(kgap)) ) {
    return ERR(-EFAULT);
  }

  if( !(to_cut = kgap.d.data.iov_len) ) {
    return 0;
  }

  if( kgap.d.data.iov_base ) {
    if( !valid_iovecs(&kgap.d.data,1,NULL) ||
        !(kgap.flags & IPC_PORT_CTL_DIR_HEAD)) {
      return ERR(-EINVAL);
    }
  }

  IPC_LOCK_PORT_W(p);
  if( !(p->flags & IPC_PORT_SHUTDOWN) &&
      (msg = __ipc_port_lookup_message(p,kgap.msg_id,&r)) &&
      message_writable(msg) && msg->extra_data ) {
    mark_message_waccess(msg);
    r=0;
  }
  IPC_UNLOCK_PORT_W(p);

  if( r ) {
    return ERR(r);
  }

  /* Make sure we fit into extra buffer. */
  to_cut = MIN(to_cut,IPC_MSG_EXTRA_SIZE - msg->extra_data_tail);

  if( to_cut && kgap.d.data.iov_base ) { /* cut-and-store */
    char *eptr = (char *)msg->extra_data + msg->extra_data_tail;

    if( copy_to_user(kgap.d.data.iov_base,eptr,to_cut) ) {
      r=-EFAULT;
    }
  }

  IPC_LOCK_PORT_W(p);
  if( !r ) {
    msg->extra_data_tail += to_cut;
    msg->data_size -= to_cut;
    r = to_cut;
  }
  unmark_message_waccess(msg);
  IPC_UNLOCK_PORT_W(p);

  return ERR(r);
}

static long __port_ctl_reply_retcode(ipc_gen_port_t *p,
                                     port_ctl_msg_extra_data_t *ugap)
{
  long r;
  port_ctl_msg_extra_data_t kgap;

  if( copy_from_user(&kgap, ugap, sizeof(kgap)) ) {
    return ERR(-EFAULT);
  }

  r=ipc_port_msg_write(p,kgap.msg_id,NULL,0,NULL,
                       0,true,kgap.d.code);

  return r >= 0 ? 0 : r;
}

long ipc_port_control(ipc_gen_port_t *p, ulong_t cmd, ulong_t arg)
{
  switch( cmd ) {
    case IPC_PORT_CTL_MSG_FWD:
      return __port_ctl_forward_message(p,(ipc_port_ctl_msg_ch_descr_t *)arg);
    case IPC_PORT_CTL_MSG_APPEND:
      return __port_ctl_append_to_message(p,(port_ctl_msg_extra_data_t *)arg);
    case IPC_PORT_CTL_MSG_CUT:
      return __port_ctl_cut_from_message(p,(port_ctl_msg_extra_data_t *)arg);
    case IPC_PORT_CTL_MSG_REPLY_RETCODE:
      return __port_ctl_reply_retcode(p,(port_ctl_msg_extra_data_t *)arg);
    default:
      break;
  }

  return ERR(-EINVAL);
}
