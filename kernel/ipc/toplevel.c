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
 * ipc/toplevel.c: Top-level entrypoints for IPC-related functions.
 */

#include <mstring/types.h>
#include <ipc/ipc.h>
#include <ipc/port.h>
#include <ipc/buffer.h>
#include <mstring/task.h>
#include <mstring/smp.h>
#include <mstring/errno.h>
#include <mstring/process.h>
#include <mstring/security.h>
#include <mstring/usercopy.h>
#include <ipc/port.h>
#include <ipc/channel.h>

bool valid_iovecs(iovec_t *iovecs, uint32_t num_vecs,long *size)
{
  int i;
  long s=0;

  for (i = 0; i < num_vecs; i++) {
    if (unlikely(!valid_user_address_range((uintptr_t)iovecs[i].iov_base, iovecs[i].iov_len))) {
      return false;
    }
    s += iovecs[i].iov_len;
  }

  if( size ) {
    *size = s;
  }

  return true;
}

/* TODO: [mt] Implement security checks for port-related syscalls ! */
long sys_open_channel(pid_t pid,ulong_t port,ulong_t flags)
{
  task_t *task = pid_to_task(pid);
  long r;

  if (task == NULL) {
    return -ESRCH;
  }

  r=ipc_open_channel(current_task(),task,port,flags);
  release_task_struct(task);
  return r;
}

int sys_close_channel(ulong_t channel)
{
  return ipc_close_channel(current_task()->ipc,channel);
}

long sys_create_port( ulong_t flags, ulong_t queue_size )
{
  task_t *caller=current_task();

  if(!trusted_task(caller)) {
    flags |= IPC_BLOCKED_ACCESS;
  }

  return ipc_create_port(caller, flags, queue_size);
}

int sys_close_port(ulong_t port)
{
  return ipc_close_port(current_task()->ipc,port);
}

static inline long __reply_iov(ulong_t port, ulong_t msg_id,
                               iovec_t reply_iov[], uint32_t numvecs)
{
  ipc_gen_port_t *p;
  long r;

  if (!valid_iovecs(reply_iov, numvecs,NULL)) {
    return -EFAULT;
  }

  p = ipc_get_port(current_task(), port);
  if (!p) {
    return -EINVAL;
  }

  r = ipc_port_reply_iov(p, msg_id, reply_iov, numvecs);
  ipc_put_port(p);
  return r;
}

long sys_port_reply_iov(ulong_t port, ulong_t msg_id,
                        iovec_t reply_iov[], uint32_t numvecs)
{
  iovec_t iovecs[MAX_IOVECS];

  if (!reply_iov || !numvecs || numvecs > MAX_IOVECS) {
    return -EINVAL;
  }
  if (copy_from_user(iovecs, reply_iov, numvecs * sizeof(iovec_t))) {
    return -EFAULT;
  }

  return __reply_iov(port, msg_id, iovecs, numvecs);
}

size_t sys_port_reply(ulong_t port,ulong_t msg_id, ulong_t reply_buf,
                      size_t reply_len)
{
  iovec_t iv;

  iv.iov_base=(void *)reply_buf;
  iv.iov_len=reply_len;

  return __reply_iov(port,msg_id,&iv,1);
}

long sys_port_receive(ulong_t port, ulong_t flags, ulong_t recv_buf,
                      size_t recv_len, port_msg_info_t *msg_info)
{
  ipc_gen_port_t *p;
  long r;
  iovec_t iovec,*piovec;

  if (recv_buf | recv_len) {
    iovec.iov_base = (void *)recv_buf;
    iovec.iov_len = recv_len;
    if (!valid_iovecs(&iovec, 1,NULL)) {
      return -EFAULT;
    }
    piovec = &iovec;
  }
  else {
    piovec = NULL;
  }

  p = ipc_get_port(current_task(), port);
  if (!p) {
    return -EINVAL;
  }

  r = ipc_port_receive(p, flags, piovec, 1, msg_info);
  ipc_put_port(p);
  return r;
}

static inline long __send_iov_v(ulong_t channel,
                                iovec_t snd_kiovecs[], uint32_t snd_numvecs,
                                iovec_t rcv_kiovecs[], uint32_t rcv_numvecs)
{
  ipc_channel_t *c;
  long ret;

  if (!valid_iovecs(snd_kiovecs, snd_numvecs,NULL)) {
    return -EFAULT;
  }

  if (rcv_kiovecs && !valid_iovecs(rcv_kiovecs, rcv_numvecs,NULL)) {
    return -EFAULT;
  }

  c = ipc_get_channel(current_task(), channel);
  if (!c) {
    return -EINVAL;
  }

  ret = ipc_port_send_iov(c, snd_kiovecs, snd_numvecs, rcv_kiovecs, rcv_numvecs);
  ipc_put_channel(c);
  return ret;
}

long sys_port_send_iov_v(ulong_t channel,
                         iovec_t snd_iov[],ulong_t snd_numvecs,
                         iovec_t rcv_iov[],ulong_t rcv_numvecs)
{
  iovec_t snd_kiovecs[MAX_IOVECS];
  iovec_t rcv_kiovecs[MAX_IOVECS];

  if (!snd_iov || !snd_numvecs || snd_numvecs > MAX_IOVECS ||
      !rcv_iov || !rcv_numvecs || rcv_numvecs > MAX_IOVECS) {
    return -EINVAL;
  }

  if (copy_from_user(&snd_kiovecs, snd_iov, snd_numvecs * sizeof(iovec_t))) {
    return -EFAULT;
  }
  if (copy_from_user(&rcv_kiovecs, rcv_iov, rcv_numvecs * sizeof(iovec_t))) {
    return -EFAULT;
  }
  
  return __send_iov_v(channel, snd_iov, snd_numvecs, rcv_iov, rcv_numvecs);
}

long sys_port_send(ulong_t channel,
                   uintptr_t snd_buf, size_t snd_size,
                   uintptr_t rcv_buf, size_t rcv_size)
{
  iovec_t snd_kiovec, rcv_kiovec;

  snd_kiovec.iov_base = (void *)snd_buf;
  snd_kiovec.iov_len = snd_size;

  rcv_kiovec.iov_base = (void *)rcv_buf;
  rcv_kiovec.iov_len = rcv_size;

  return __send_iov_v(channel, &snd_kiovec, 1, &rcv_kiovec, 1);
}

long sys_port_send_iov(ulong_t channel, iovec_t iov[], uint32_t numvecs,
                       uintptr_t rcv_buf, size_t rcv_size)
{
  iovec_t kiovecs[MAX_IOVECS], rcv_kiovec;

  if (!iov || !numvecs || numvecs > MAX_IOVECS) {
    return -EINVAL;
  }

  if (copy_from_user(kiovecs, iov, numvecs * sizeof(iovec_t))) {
    return -EFAULT;
  }

  rcv_kiovec.iov_base = (void *)rcv_buf;
  rcv_kiovec.iov_len = rcv_size;

  return __send_iov_v(channel, kiovecs, numvecs, &rcv_kiovec, 1);
}

long sys_control_channel(ulong_t channel, ulong_t cmd, ulong_t arg)
{
  return ipc_channel_control(current_task(),channel,cmd,arg);
}

long sys_port_msg_read(ulong_t port, ulong_t msg_id, uintptr_t recv_buf,
                       size_t recv_len, off_t offset)
{
  ipc_gen_port_t *p;
  long r;
  iovec_t iovec;

  p = ipc_get_port(current_task(), port);
  if (!p) {
    return -EINVAL;
  }

  iovec.iov_base = (void *)recv_buf;
  iovec.iov_len = recv_len;
  if (!valid_iovecs(&iovec, 1, NULL)) {
    return -EFAULT;
  }

  r = ipc_port_msg_read(p, msg_id, &iovec, 1, offset);
  ipc_put_port(p);
  return r;
}

long sys_port_control(ulong_t port, ulong_t cmd, ulong_t arg)
{
  ipc_gen_port_t *p;
  long r;

  p = ipc_get_port(current_task(), port);
  if (!p) {
    return -EINVAL;
  }

  r = ipc_port_control(p, cmd, arg);
  ipc_put_port(p);
  return r;
}

long sys_port_msg_write(ulong_t port, ulong_t msg_id, iovec_t *iovecs,
                        ulong_t numvecs,off_t offset)
{
  ipc_gen_port_t *p;
  long r;
  iovec_t kiovecs[MAX_IOVECS];
  long size,processed;
  off_t koffset;

  if( !numvecs || numvecs > IPC_ABS_IOVEC_LIM ) {
    return ERR(-EINVAL);
  }

  p = ipc_get_port(current_task(), port);
  if( !p ) {
    r=-EINVAL;
  } else {
    processed = 0;
    koffset = offset;

    while( numvecs ) {
      ulong_t nc = MIN(numvecs,MAX_IOVECS);

      if( copy_from_user(kiovecs,iovecs,nc*sizeof(iovec_t)) ||
          !valid_iovecs(kiovecs,nc,&size) ) {
        r=-EFAULT;
        break;
      }

      r=ipc_port_msg_write(p,msg_id,kiovecs,nc,&koffset,size);
      if( r < 0 ) {
        processed = r;
        break;
      }

      processed += r;
      numvecs -= nc;
      iovecs += nc;
    }
  }

  ipc_put_port(p);
  return ERR(processed);
}
