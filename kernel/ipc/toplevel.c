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
 * (c) Copyright 2006,2007,2008,2009 MString Core Team <http://mstring.jarios.org>
 * (c) Copyright 2008,2009 Michael Tsymbalyuk <mtzaurus@gmail.com>
 * (c) Copyright 2010 Jari OS non-profit org <http://jarios.org>
 * (c) Copyright 2010 MadTirra <madtirra@jarios.org> - namespace related changes
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
#include <mstring/usercopy.h>
#include <ipc/port.h>
#include <ipc/channel.h>
#include <security/security.h>
#include <config.h>

static long __ipc_port_msg_write(ulong_t port, ulong_t msg_id, iovec_t *iovecs,
                                 ulong_t numvecs,off_t offset, bool wakeup,
                                 bool map_rcv_buffer)
{
  ipc_gen_port_t *p;
  long r;
  iovec_t kiovecs[MAX_IOVECS];
  long size,processed = 0;
  off_t koffset;

  if( !numvecs || numvecs > IPC_ABS_IOVEC_LIM ) {
    return ERR(-EINVAL);
  }

  if( (p=ipc_get_port(current_task(), port,&r)) ) {
    processed = 0;
    koffset = offset;

    while( numvecs ) {
      ulong_t nc = MIN(numvecs,MAX_IOVECS);

      if( copy_from_user(kiovecs,iovecs,nc*sizeof(iovec_t)) ||
          !valid_iovecs(kiovecs,nc,&size) ) {
        r=-EFAULT;
        break;
      }

      /* Wake up sender only when processing the last set of I/O vecs. */
      r=ipc_port_msg_write(p,msg_id,kiovecs,nc,&koffset,size,
                           (numvecs <= MAX_IOVECS && wakeup), processed,
                           map_rcv_buffer);
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

long sys_open_channel(pid_t pid,ulong_t port,ulong_t flags)
{
  task_t *task = pid_to_task(pid);
#ifdef CONFIG_ENABLE_DOMAIN
  task_t *ctask;
#endif
  long r;

  if( !s_check_system_capability(SYS_CAP_IPC_CHANNEL) ) {
    return ERR(-EPERM);
  }

  if (task == NULL) {
    return ERR(-ESRCH);
  }

#ifdef CONFIG_ENABLE_DOMAIN
  /* check for namespace */
  ctask = current_task();
  if((task->domain->dm_id != ctask->domain->dm_id) &&
     !ctask->domain->trans_flag)
    return ERR(-EPERM);
#endif

  r=ipc_open_channel(current_task(),task,port,flags);
  release_task_struct(task);
  return ERR(r);
}

int sys_close_channel(ulong_t channel)
{
  long r=ipc_close_channel(current_task()->ipc,channel);
  return ERR(r);
}

long sys_create_port( ulong_t flags, ulong_t queue_size )
{
  task_t *caller=current_task();
  long r;

  if( !s_check_system_capability(SYS_CAP_IPC_PORT) ) {
    return ERR(-EPERM);
  }

  /* TODO: [mt] Check if caller can create unblocked ports. */
  //flags |= IPC_BLOCKED_ACCESS;

  r=ipc_create_port(caller, flags, queue_size);
  return ERR(r);
}

int sys_close_port(ulong_t port)
{
  return ipc_close_port(current_task()->ipc,port);
}

long sys_port_reply_iov(ulong_t port, ulong_t msg_id,
                        iovec_t reply_iov[], uint32_t numvecs)
{
  long r = __ipc_port_msg_write(port,msg_id,reply_iov,numvecs,0,true,false);
  return r > 0 ? 0 : ERR(r);
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
      return ERR(-EFAULT);
    }
    piovec = &iovec;
  }
  else {
    piovec = NULL;
  }

  if ( (p=ipc_get_port(current_task(), port, &r)) ) {
    r=ipc_port_receive(p, flags, piovec, 1, msg_info);
    ipc_put_port(p);
  }

  return ERR(r);
}

long sys_port_send_iov_v(ulong_t channel,
                         iovec_t snd_iov[],ulong_t snd_numvecs,
                         iovec_t rcv_iov[],ulong_t rcv_numvecs)
{
  iovec_t snd_kiovecs[MAX_IOVECS];
  iovec_t rcv_kiovecs[MAX_IOVECS];
  ipc_channel_t *c;
  long ret;

  if (!snd_iov || !snd_numvecs || snd_numvecs > MAX_IOVECS ||
      !rcv_iov || !rcv_numvecs || rcv_numvecs > MAX_IOVECS) {
    return ERR(-EINVAL);
  }

  if (copy_from_user(&snd_kiovecs, snd_iov, snd_numvecs * sizeof(iovec_t))) {
    return ERR(-EFAULT);
  }

  if (copy_from_user(&rcv_kiovecs, rcv_iov, rcv_numvecs * sizeof(iovec_t))) {
    return ERR(-EFAULT);
  }

  if (!valid_iovecs(snd_kiovecs, snd_numvecs,NULL) ||
      !valid_iovecs(rcv_kiovecs, rcv_numvecs,NULL) ) {
    return ERR(-EFAULT);
  }

  c = ipc_get_channel(current_task(), channel);
  if (!c) {
    return ERR(-EINVAL);
  }

  ret = ipc_port_send_iov(c, snd_kiovecs, snd_numvecs, rcv_kiovecs, rcv_numvecs);
  ipc_put_channel(c);

  return ERR(ret);
}

long sys_control_channel(ulong_t channel, ulong_t cmd, ulong_t arg)
{
  long r=ipc_channel_control(current_task(),channel,cmd,arg);
  return ERR(r);
}

long sys_port_msg_read(ulong_t port, ulong_t msg_id, uintptr_t recv_buf,
                       size_t recv_len, off_t offset)
{
  ipc_gen_port_t *p;
  long r;
  iovec_t iovec;

  if (!(p=ipc_get_port(current_task(), port, &r))) {
    return ERR(r);
  }

  iovec.iov_base = (void *)recv_buf;
  iovec.iov_len = recv_len;
  if (!valid_iovecs(&iovec, 1, NULL)) {
    return ERR(-EFAULT);
  }

  r = ipc_port_msg_read(p, msg_id, &iovec, 1, offset);
  ipc_put_port(p);
  return ERR(r);
}

long sys_port_control(ulong_t port, ulong_t cmd, ulong_t arg)
{
  ipc_gen_port_t *p;
  long r;

  if( !s_check_system_capability(SYS_CAP_IPC_CONTROL) ) {
    return ERR(-EPERM);
  }

  if ( !(p=ipc_get_port(current_task(), port,&r)) ) {
    return ERR(r);
  }

  r=ipc_port_control(p, cmd, arg);
  ipc_put_port(p);
  return ERR(r);
}

long sys_port_msg_write(ulong_t port, ulong_t msg_id, iovec_t *iovecs,
                                 ulong_t numvecs,off_t offset)
{
  long r=__ipc_port_msg_write(port,msg_id,iovecs,numvecs,offset,false,true);
  return ERR(r);
}
