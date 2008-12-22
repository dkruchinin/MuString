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
 * ipc/toplevel.c: Top-level entrypoints for IPC-related functions.
 */

#include <eza/arch/types.h>
#include <ipc/ipc.h>
#include <ipc/port.h>
#include <ipc/buffer.h>
#include <eza/task.h>
#include <eza/smp.h>
#include <kernel/syscalls.h>
#include <eza/errno.h>
#include <eza/process.h>
#include <eza/security.h>
#include <ipc/gen_port.h>
#include <ipc/channel.h>
#include <kernel/vm.h>

/* TODO: [mt] Implement security checks for port-related syscalls ! */
status_t sys_open_channel(pid_t pid,ulong_t port,ulong_t flags)
{
  task_t *task = pid_to_task(pid);
  status_t r;

  if( task == NULL ) {
    return -ESRCH;
  }

  r=ipc_open_channel(current_task(),task,port,flags);
  release_task_struct(task);
  return r;
}

status_t sys_close_channel(ulong_t channel)
{
  return ipc_close_channel(current_task(),channel);
}

status_t sys_create_port( ulong_t flags, ulong_t queue_size )
{
  task_t *caller=current_task();

  if( !trusted_task(caller) ) {
    flags |= IPC_BLOCKED_ACCESS;
  }

  return __ipc_create_port(caller,flags);
}

status_t sys_close_port(ulong_t port)
{
  return ipc_close_port(current_task(),port);
}

static status_t __reply_iov(ulong_t port,ulong_t msg_id,
                            iovec_t reply_iov[],ulong_t numvecs)
{
  ipc_gen_port_t *p;
  int i,reply_size;

  for(reply_size=0,i=0;i<snd_numvecs;i++) {
    if( !valid_user_address_range(reply_iov[i].iov_base,
                                  snd_kiovecs[i].iov_len) ) {
      return -EFAULT;
    }

    msg_size += snd_kiovecs[i].iov_len;
    if( msg_size > MAX_PORT_MSG_LENGTH ) {
      return -EINVAL;
    }
  }

  p=__ipc_get_port(current_task(),port);
  if( !p ) {
    return -EINVAL;
  }

  r=__ipc_port_reply(p,msg_id,reply_buf,reply_len);
  __ipc_put_port(p);
  return r;
}

status_t sys_port_reply_iov(ulong_t port,ulong_t msg_id,
                            iovec_t reply_iov[],ulong_t numvecs) {
  iovec_t iovecs[MAX_IOVECS];

  if( !reply_iov || !numvecs || numvecs > MAX_IOVECS ) {
    return -EINVAL;
  }

  if( copy_from_user(iovecs,reply_iov,numvecs*sizeof(iovec_t)) ) {
    return -EFAULT;
  }
  return __reply_iov(port,msg_id,iovecs,numvecs);
}

status_t sys_port_reply(ulong_t port,ulong_t msg_id,ulong_t reply_buf,
                        ulong_t reply_len) {
  iovec_t iv;

  iv.iov_base=reply_buf;
  iv.iov_len=reply_len;

  return __reply_iov(port,msg_id,&iv,1);
}

status_t sys_port_receive(ulong_t port, ulong_t flags, ulong_t recv_buf,
                          ulong_t recv_len,port_msg_info_t *msg_info)
{
  ipc_gen_port_t *p;
  status_t r;

  if( !valid_user_address_range((ulong_t)msg_info,sizeof(*msg_info)) ||
      !valid_user_address_range(recv_buf,recv_len) ) {
    return -EFAULT;
  }

  p=__ipc_get_port(current_task(),port);
  if( !p ) {
    return -EINVAL;
  }

  r=__ipc_port_receive(p,flags,recv_buf,recv_len,msg_info);
  __ipc_put_port(p);
  return r;
}

status_t sys_port_send(ulong_t channel,
                       uintptr_t snd_buf,ulong_t snd_size,
                       uintptr_t rcv_buf,ulong_t rcv_size)
{
  task_t *caller=current_task();
  ipc_channel_t *c=ipc_get_channel(caller,channel);
  ipc_gen_port_t *port;
  ipc_port_message_t *msg;
  status_t r=-EINVAL;
  iovec_t kiovec;

  if( c == NULL ) {
    return -EINVAL;
  }

  if( !valid_user_address_range((ulong_t)snd_buf,snd_size) ) {
    return -EFAULT;
  }

  r=ipc_get_channel_port(c,&port);
  if( r ) {
    goto put_channel;
  }

  kiovec.iov_base=(void *)snd_buf;
  kiovec.iov_len=snd_size;

  msg=ipc_create_port_message_iov(&kiovec,1,snd_size,
                                  channel_in_blocked_mode(c),rcv_buf,rcv_size);
  if( !msg ) {
    r=-ENOMEM;
  } else {
    r=__ipc_port_send(port,msg,channel_in_blocked_mode(c),rcv_buf,rcv_size);
  }

  __ipc_put_port(port);
put_channel:
  ipc_put_channel(c);
  return r;
}

status_t sys_port_send_iov_v(ulong_t channel,
                             iovec_t snd_iov[],ulong_t snd_numvecs,
                             iovec_t rcv_iov[],ulong_t rcv_numvecs)
{
  status_t r;
  iovec_t snd_kiovecs[MAX_IOVECS];
  iovec_t rcv_kiovecs[MAX_IOVECS];
  ulong_t i,msg_size,rcv_size;
  task_t *caller=current_task();
  ipc_channel_t *c;
  ipc_gen_port_t *port;
  ipc_port_message_t *msg;
  ipc_user_buffer_t snd_bufs[MAX_IOVECS],rcv_bufs[MAX_IOVECS];

  if( !snd_iov || !snd_numvecs || snd_numvecs > MAX_IOVECS ||
      !rcv_iov || !rcv_numvecs || rcv_numvecs > MAX_IOVECS ) {
    return -EINVAL;
  }

  if( copy_from_user( &snd_kiovecs,snd_iov,snd_numvecs*sizeof(iovec_t)) ) {
    return -EFAULT;
  }

  if( copy_from_user( &rcv_kiovecs,rcv_iov,rcv_numvecs*sizeof(iovec_t)) ) {
    return -EFAULT;
  }

  /* Check send buffer. */
  for(msg_size=0,i=0;i<snd_numvecs;i++) {
    if( !valid_user_address_range(snd_kiovecs[i].iov_base,
                                  snd_kiovecs[i].iov_len) ) {
      return -EFAULT;
    }

    msg_size += snd_kiovecs[i].iov_len;
    if( msg_size > MAX_PORT_MSG_LENGTH ) {
      return -EINVAL;
    }
  }

  /* Check receive buffer. */
  for(rcv_size=0,i=0;i<rcv_numvecs;i++) {
    if( !valid_user_address_range(rcv_kiovecs[i].iov_base,
                                  rcv_kiovecs[i].iov_len) ) {
      return -EFAULT;
    }

    rcv_size += rcv_kiovecs[i].iov_len;
    if( rcv_size > MAX_PORT_MSG_LENGTH ) {
      return -EINVAL;
    }
  }

  c=ipc_get_channel(caller,channel);
  if( !c ) {
    return -EINVAL;
  }

  r=ipc_get_channel_port(c,&port);
  if( r ) {
    goto put_channel;
  }

  msg=ipc_create_port_message_iov_v(snd_kiovecs,snd_numvecs,
                                    msg_size,channel_in_blocked_mode(c),
                                    rcv_kiovecs,rcv_numvecs,
                                    snd_bufs,rcv_bufs,rcv_size);
  if( !msg ) {
    r=-ENOMEM;
  } else {
//    r=__ipc_port_send(port,msg,channel_in_blocked_mode(c),rcv_buf,rcv_size);
  }

  __ipc_put_port(port);
put_channel:
  ipc_put_channel(c);
  return r;

  return r;
}

status_t sys_port_send_iov(ulong_t channel,
                           iovec_t iov[],ulong_t numvecs,
                           uintptr_t rcv_buf,ulong_t rcv_size)
{
  status_t r;
  iovec_t kiovecs[MAX_IOVECS];
  ulong_t i,msg_size;
  task_t *caller=current_task();
  ipc_channel_t *c;
  ipc_gen_port_t *port;
  ipc_port_message_t *msg;

  if( !iov || !numvecs || numvecs > MAX_IOVECS ) {
    return -EINVAL;
  }

  if( copy_from_user( kiovecs,iov,numvecs*sizeof(iovec_t)) ) {
    return -EFAULT;
  }

  for(msg_size=0,i=0;i<numvecs;i++) {
    if( !valid_user_address_range(kiovecs[i].iov_base,
                                  kiovecs[i].iov_len) ) {
      return -EFAULT;
    }
    msg_size += kiovecs[i].iov_len;
    if( msg_size > MAX_PORT_MSG_LENGTH ) {
      return -EINVAL;
    }
  }

  c=ipc_get_channel(caller,channel);
  if( !c ) {
    return -EINVAL;
  }

  r=ipc_get_channel_port(c,&port);
  if( r ) {
    goto put_channel;
  }

  msg=ipc_create_port_message_iov(kiovecs,numvecs,msg_size,
                                  channel_in_blocked_mode(c),rcv_buf,rcv_size);
  if( !msg ) {
    r=-ENOMEM;
  } else {
    r=__ipc_port_send(port,msg,channel_in_blocked_mode(c),rcv_buf,rcv_size);
  }

  __ipc_put_port(port);
put_channel:
  ipc_put_channel(c);
  return r;
}

status_t sys_control_channel(ulong_t channel,ulong_t cmd,ulong_t arg)
{
  return ipc_channel_control(current_task(),channel,cmd,arg);
}
