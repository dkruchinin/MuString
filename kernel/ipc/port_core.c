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
 * ipc/port_core.c: implementation of core IPC ports logic.
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
#include <mm/page_alloc.h>
#include <security/util.h>

#define POST_MESSAGE_DATA_ACCESS_STEP(_p,_m,_r,_woe)    \
  IPC_LOCK_PORT_W(_p);                             \
  (_m)->state=MSG_STATE_DATA_TRANSFERRED;          \
                                                   \
  if( task_was_interrupted((_m)->sender) ) {       \
    (_r)=-EPIPE;                                   \
  }                                                \
                                                   \
  if( (_r) && (_woe) && !((_p)->flags & IPC_PORT_SHUTDOWN) ) {          \
    (_p)->msg_ops->remove_message((_p),(_m));                           \
    (_m)->replied_size=(_r);                                            \
    event_raise(&(_m)->event);                                          \
  }                                                                     \
  IPC_UNLOCK_PORT_W((_p));

#define IPC_INIT_PORT(p)                        \
  memset((p),0,sizeof(ipc_gen_port_t));         \
  atomic_set(&(p)->use_count,1);                \
  atomic_set(&(p)->own_count,1);                \
  spinlock_initialize(&(p)->lock);              \
  (p)->avail_messages=(p)->total_messages=0;    \
  list_init_head(&(p)->channels);               \
  waitqueue_initialize(&(p)->waitqueue)

static int __calc_msg_length(iovec_t iovecs[], ulong_t num_iovecs, ulong_t *size)
{
  ulong_t i, msg_size;
  
  ASSERT(size != NULL);
  for (msg_size = 0, i = 0; i < num_iovecs; i++) {
    msg_size += iovecs[i].iov_len;
    if (msg_size > MAX_PORT_MSG_LENGTH)
      return ERR(-E2BIG);
  }

  *size = msg_size;
  return 0;
}

static ipc_port_message_t *__ipc_create_nb_port_message(ipc_channel_t *channel,uintptr_t snd_buf,
                                                 ulong_t snd_size,bool copy_data)
{
  if( snd_size <= IPC_NB_MESSAGE_MAXLEN && snd_size ) {
    ipc_port_message_t *msg=memalloc(sizeof(*msg)+snd_size);
    if( msg ) {
      memset(msg,0,sizeof(*msg));

      IPC_RESET_MESSAGE(msg,current_task());
      msg->send_buffer=(void *)((char *)msg+sizeof(*msg));
      msg->data_size=snd_size;
      msg->reply_size=0;
      msg->sender=current_task();
      msg->blocked_mode = false;

      if( copy_data ) {
        if( !copy_from_user(msg->send_buffer,(void *)snd_buf,snd_size) ) {
          return msg;
        } else {
          memfree(msg);
        }
      } else {
        return msg;
      }
    }
  }
  return NULL;
}

void put_ipc_port_message(ipc_port_message_t *msg)
{
  void *extra_data = msg->extra_data;

  if (msg->blocked_mode) {
    if (msg->snd_buf && (msg->data_size > IPC_BUFFERED_PORT_LENGTH)) {
      ipc_release_buffer_pages(msg->snd_buf, msg->num_send_bufs);
    }
    if (msg->rcv_buf && (msg->reply_size > IPC_BUFFERED_PORT_LENGTH)) {
      ipc_release_buffer_pages(msg->rcv_buf, msg->num_recv_buffers);
    }
  }
  else {
    memfree(msg);
  }

  if( extra_data ) {
    free_pages_addr(extra_data,IPC_MSG_EXTRA_PAGES);
  }
}

ipc_port_message_t *ipc_create_port_message_iov_v(ipc_channel_t *channel, iovec_t *snd_kiovecs, uint32_t snd_numvecs,
                                                  size_t data_len, iovec_t *rcv_kiovecs, uint32_t rcv_numvecs,
                                                  ipc_buffer_t *snd_bufs, ipc_buffer_t *rcv_bufs, size_t rcv_size)
{
  ipc_port_message_t *msg;
  int i, r;
  char *p;
  task_ipc_priv_t *ipc_priv;
  task_t *owner = current_task();

  ipc_priv = owner->ipc_priv;
  if (!channel_in_blocked_mode(channel)) {
    if (data_len > IPC_NB_MESSAGE_MAXLEN) {
      return NULL;
    }

    msg= __ipc_create_nb_port_message(channel, 0, data_len, false);
    if (!msg) {
      return NULL;
    }
  }
  else {
    msg = &owner->ipc_priv->cached_data.cached_port_message;
    IPC_RESET_MESSAGE(msg, owner);

    msg->blocked_mode = true;
    msg->data_size = data_len;
    msg->reply_size = rcv_size;
    msg->sender = owner;

    /* Prepare send buffer. */
    if (data_len <= IPC_BUFFERED_PORT_LENGTH) {
      msg->send_buffer = ipc_priv->cached_data.cached_page1;
      msg->num_send_bufs = 0;
    }
    else {
      /* Well, need to setup user buffers. */
      r = ipc_setup_buffer_pages(snd_kiovecs, snd_numvecs,
                                 (page_idx_t *)ipc_priv->cached_data.cached_page1,
                                 snd_bufs, true);
      if (r) {
        goto free_message;
      }
      
      msg->num_send_bufs = snd_numvecs;
      msg->snd_buf = snd_bufs;
    }

    /* Prepare receive buffer. */
    if (rcv_size) {
      if (rcv_size <= IPC_BUFFERED_PORT_LENGTH) {
        msg->receive_buffer = ipc_priv->cached_data.cached_page2;
        msg->rcv_buf = NULL;
        msg->num_recv_buffers = 0;
      }
      else {
        r = ipc_setup_buffer_pages(rcv_kiovecs, rcv_numvecs,
                                   (page_idx_t *)ipc_priv->cached_data.cached_page2,
                                   rcv_bufs, false);
        if (r) {
          goto free_message;
        }
        
        msg->rcv_buf = rcv_bufs;
        msg->num_recv_buffers = rcv_numvecs;
        /* , Fallthrough. */
      }
    }
  }

  /* Now copy user data to the message. */
  if (data_len <= IPC_BUFFERED_PORT_LENGTH) {
    p = msg->send_buffer;
    for (i = 0; i < snd_numvecs; i++) {
      if( kernel_channel(channel) ) {
        memcpy(p,snd_kiovecs->iov_base,snd_kiovecs->iov_len);
      } else {
        if (copy_from_user(p, snd_kiovecs->iov_base, snd_kiovecs->iov_len)) {
          goto free_message;
        }
      }
      p += snd_kiovecs->iov_len;
      snd_kiovecs++;
    }
  }
  
  return msg;
free_message:
  put_ipc_port_message(msg);
  return NULL;
}

static int __allocate_port(ipc_gen_port_t **out_port, ulong_t flags,
                           ulong_t queue_size, task_t *owner)
{
  ipc_gen_port_t *p = memalloc(sizeof(*p));
  int r;

  if(p == NULL) {
    return -ENOMEM;
  }

  IPC_INIT_PORT(p);

  p->msg_ops = &prio_port_msg_ops;
  p->port_ops = &prio_port_ops;
  p->flags = (flags & IPC_PORT_DIRECT_FLAGS);
  r=p->msg_ops->init_data_storage(p,owner,queue_size);
  if( r ) {
    goto out_free_port;
  }

  p->flags = flags;
  *out_port = p;
  return 0;
out_free_port:
  memfree(p);
  return r;
}

ipc_gen_port_t *ipc_clone_port(ipc_gen_port_t *p)
{
  ipc_gen_port_t *port=memalloc(sizeof(*p));

  if( port ) {
    IPC_INIT_PORT(port);
    port->flags=p->flags;
    port->msg_ops=p->msg_ops;
    port->port_ops=p->port_ops;

    if( port->msg_ops->init_data_storage(port,current_task(),
                                         p->capacity) ) {
      memfree(port);
      port=NULL;
    }
  }

  return port;
}

static void __shutdown_port(ipc_gen_port_t *port)
{
  ipc_port_message_t *msg;

repeat:
  IPC_LOCK_PORT_W(port);
  msg=port->msg_ops->remove_head_message(port);
  if( msg ) {
    if( message_writable(msg) ) {
      msg->replied_size=-EPIPE;
      event_raise(&msg->event);
    }
    IPC_UNLOCK_PORT_W(port);
    goto repeat;
  }
  IPC_UNLOCK_PORT_W(port);
}

static long __transfer_reply_data_iov(ipc_port_message_t *msg,
                                      iovec_t *reply_iov,ulong_t numvecs,
                                      bool from_server,ulong_t reply_len,
                                      ulong_t offset)
{
  long r=0,processed=0;
  ulong_t i,to_copy,rlen;
  char *rcv_buf;

  if( from_server ) {
    reply_len=MIN(reply_len,msg->reply_size-offset);
  } else {
    reply_len=MIN(reply_len,msg->replied_size);
  }

  if( reply_len > 0 ) {
    if( msg->reply_size <= IPC_BUFFERED_PORT_LENGTH ) {
      /* Short message - copy it from the buffer. */
      rcv_buf=msg->receive_buffer + offset;

      for(i=0,rlen=reply_len;i<numvecs && rlen;i++,reply_iov++) {
        to_copy=MIN(rlen,reply_iov->iov_len);

        if( from_server ) {          
          r=copy_from_user(rcv_buf,reply_iov->iov_base,to_copy);
        } else {
          r=copy_to_user(reply_iov->iov_base,rcv_buf,to_copy);
        }

        if( r ) {
          break;
        }
        rlen-=to_copy;
        rcv_buf += to_copy;
        processed += to_copy;
      }
    } else {
      /* Long message - process it via buffer. */
      if( from_server ) {
        r=ipc_transfer_buffer_data_iov(msg->rcv_buf,msg->num_recv_buffers,
                                       reply_iov,numvecs,offset,true);
        processed = r;
        if( r >= 0 ) {
          r=0;
        }
      } else {
        r=0;
      }
    }
  }

  if( r ) {
    r=reply_len=-EFAULT;
  }

  /* If we're replying to the message, setup size properly. */
  if( from_server ) {
    msg->replied_size=reply_len;
  }
  return r ? r : processed;
}

static long __transfer_message_data_to_receiver(ipc_port_message_t *msg,
                                                iovec_t *iovec, ulong_t numvecs,
                                                port_msg_info_t *stats,
                                                ulong_t offset)
{
  long r;
  size_t recv_len;

  if( iovec ) {
    if( offset >= msg->data_size ) {
      return -EINVAL;
    }

    if( msg->extra_data && (offset < (IPC_MSG_EXTRA_SIZE - msg->extra_data_tail)) ) {
      char *eptr = (char *)msg->extra_data + msg->extra_data_tail + offset;
      recv_len=MIN(iovec->iov_len,(IPC_MSG_EXTRA_SIZE - msg->extra_data_tail));

      if( copy_to_user(iovec->iov_base,eptr,recv_len) ) {
        r=-EFAULT;
        goto out;
      }

      iovec->iov_len -= recv_len;
      iovec->iov_base = (void *)((char *)iovec->iov_base + recv_len);
      offset = 0;
    }

    if( !iovec->iov_len ) {
      r=0;
      goto out;
    }

    recv_len=MIN(iovec->iov_len,msg->data_size-offset);
    if( msg->data_size <= IPC_BUFFERED_PORT_LENGTH ) {
      /* Short message - copy it from the send buffer.
       */
      r=copy_to_user((void *)iovec->iov_base,msg->send_buffer+offset,recv_len);
      if( r > 0 ) {
        r=-EFAULT;
      }
    } else {
      /* Long message - process it via IPC buffers.
       */
      r=ipc_transfer_buffer_data_iov(msg->snd_buf,msg->num_send_bufs,
                                     iovec,numvecs,offset,false);
      if( r > 0 ) {
        r=0;
      }
    }
  } else {
    r=0;
  }

out:
  if( !r && stats ) {
    port_msg_info_t info;

    info.sender_pid=msg->sender->pid;
    info.msg_id=msg->id;
    info.msg_len=msg->data_size;
    info.sender_tid=msg->sender->tid;
    info.sender_uid=msg->sender->uid;
    info.sender_gid=msg->sender->gid;

    if( copy_to_user(stats,&info,sizeof(info)) ) {
      r=-EFAULT;
    }
  }

  return r;
}

int ipc_close_port(task_ipc_t *ipc,ulong_t port)
{
  ipc_gen_port_t *p = NULL;
  bool shutdown=false;
  int r;

  if( !ipc ) {
    return -EINVAL;
  }

  LOCK_IPC(ipc);
  if( !ipc->ports || port > ipc->max_port_num ) {
    r=-EMFILE;
    goto out_unlock;
  }

  IPC_LOCK_PORTS(ipc);
  p=ipc->ports[port];
  if( p ) {
    IPC_LOCK_PORT_W(p);
    shutdown=atomic_dec_and_test(&p->own_count);
    if(shutdown) {
      p->flags |= IPC_PORT_SHUTDOWN;
      ipc->ports[port]=NULL;
    }
    IPC_UNLOCK_PORT_W(p);
  }
  IPC_UNLOCK_PORTS(ipc);

  if( p ) {
    if( shutdown ) {
      /* For better efficiency wi will actually shutdown port after
       * unlocking the IPC lock.
       */
      idx_free(&ipc->ports_array,port);
      ipc->num_ports--;
    }
    r=0;
  } else {
    r=-EINVAL;
  }
out_unlock:
  UNLOCK_IPC(ipc);
  if( p ) {
    if( shutdown ) {
      __shutdown_port(p);
    }
    ipc_put_port(p);
  }
  return r;
}

long ipc_create_port(task_t *owner,ulong_t flags,ulong_t queue_size)
{
  long r;
  task_ipc_t *ipc = get_task_ipc(owner);
  ulong_t id;
  ipc_gen_port_t *port = NULL;

  if( !ipc ) {
    return -EINVAL;
  }

  LOCK_IPC(ipc);

  if(ipc->num_ports >= owner->limits->limits[LIMIT_IPC_MAX_PORTS]) {
    r=-EMFILE;
    goto out_unlock;
  }

  if( !queue_size ) {  /* Default queue size. */
    queue_size=owner->limits->limits[LIMIT_IPC_MAX_PORT_MESSAGES];
  }

  if( queue_size > owner->limits->limits[LIMIT_IPC_MAX_PORT_MESSAGES] ) {
    r=-EINVAL;
    goto out_unlock;
  }

  /* First port created ? */
  if( !ipc->ports ) {
    r = -ENOMEM;
    ipc->ports=allocate_ipc_memory(sizeof(ipc_gen_port_t *)*CONFIG_IPC_DEFAULT_PORTS);
    if( !ipc->ports ) {
      goto out_unlock;
    }
    ipc->allocated_ports=CONFIG_IPC_DEFAULT_PORTS;
  } else if( ipc->num_ports >= ipc->allocated_ports ) {
    r=-EMFILE;
    goto out_unlock;
  }

  id = idx_allocate(&ipc->ports_array);
  if( id == IDX_INVAL ) {
    r = -EMFILE;
    goto out_unlock;
  }

  /* Ok, it seems that we can create a new port. */
  r = __allocate_port(&port,flags,queue_size,owner);
  if( r != 0 ) {
    goto free_id;
  }

  s_copy_mac_label(S_GET_INVOKER(),S_GET_PORT_OBJ(port));

  /* Install new port. */
  IPC_LOCK_PORTS(ipc);
  ipc->ports[id] = port;
  ipc->num_ports++;

  if( id > ipc->max_port_num ) {
    ipc->max_port_num=id;
  }
  IPC_UNLOCK_PORTS(ipc);

  r = id;
  UNLOCK_IPC(ipc);
  release_task_ipc(ipc);
  return r;
free_id:
  idx_free(&ipc->ports_array,id);
out_unlock:
  UNLOCK_IPC(ipc);
  release_task_ipc(ipc);
  return r;
}

long ipc_port_msg_read(struct __ipc_gen_port *port,ulong_t msg_id,
                       iovec_t *rcv_iov,ulong_t numvecs,ulong_t offset) {
  ipc_port_message_t *msg = NULL;
  long r = 0;

  IPC_LOCK_PORT_W(port);
  if( !(port->flags & IPC_PORT_SHUTDOWN) ) {
    msg=port->msg_ops->lookup_message(port,msg_id);
    if( msg == __MSG_WAS_DEQUEUED ) { /* Client got lost. */
      r=-ENXIO;
    } else if( msg ) {
      msg->state=MSG_STATE_DATA_UNDER_ACCESS;
      r=0;
    } else {
      r=-EINVAL;
    }
  }
  IPC_UNLOCK_PORT_W(port);

  if( !r ) {
    r=__transfer_message_data_to_receiver(msg,rcv_iov,numvecs,NULL,offset);
    POST_MESSAGE_DATA_ACCESS_STEP(port,msg,r,false);
  }

  return r;
}

long ipc_port_msg_write(struct __ipc_gen_port *port,ulong_t msg_id,
                        struct __iovec *iovecs,ulong_t numvecs,off_t *poffset,
                        long len, bool wakeup,long msg_size)
{
  ipc_port_message_t *msg;
  long r = -EINVAL;

  IPC_LOCK_PORT_W(port);
  if( (msg = __ipc_port_lookup_message(port,msg_id,&r)) ) {
    if( message_writable(msg) ) {
      if( !poffset || (*poffset < msg->reply_size) ) {
        r=0;
      }
    }
  }

  if( !r ) {
    if( wakeup ) {
      r=port->msg_ops->remove_message(port,msg);
      msg->state=MSG_STATE_REPLY_BEGIN;
    } else {
      mark_message_waccess(msg);
    }
  }
  IPC_UNLOCK_PORT_W(port);

  if( !r ) {
    if( iovecs ) {
      r=__transfer_reply_data_iov(msg,iovecs,numvecs,true,len,*poffset);
    }

    IPC_LOCK_PORT_W(port);
    unmark_message_waccess(msg);
    IPC_UNLOCK_PORT_W(port);

    if( r > 0 && poffset ) {
      *poffset +=r;
    }

    if( wakeup ) {
      if( (port->flags & IPC_AUTOREF) && msg->sender ) {
        release_task_struct(msg->sender);
      }

      /* Message was removed from the port queue, so we don't need
       * any locks when accessing it in case when reply was requested.
       */
      msg->replied_size=(r >= 0) ? msg_size + r : r;
      msg->state=MSG_STATE_REPLIED;
      event_raise(&msg->event);
    }
  }
  return ERR(r);
}

long ipc_port_receive(ipc_gen_port_t *port, ulong_t flags,
                      iovec_t *iovec, uint32_t numvec,
                      port_msg_info_t *msg_info)
{
  long r=-EINVAL;
  ipc_port_message_t *msg;
  task_t *owner=current_task();

  if (!msg_info) {
    return -EINVAL;
  }

recv_cycle:
  /* Main 'Receive' cycle. */
  msg=NULL;
  if( task_was_interrupted(owner) ) {
    return -EINTR;
  }

  IPC_LOCK_PORT_W(port);
  if( !(port->flags & IPC_PORT_SHUTDOWN) ) {
    if( !port->avail_messages ) {
      /* No incoming messages: sleep (if requested). */
      if( flags & IPC_BLOCKED_ACCESS ) {
        wqueue_task_t w;

      /* No luck: need to sleep. We don't unlock the port right now
       * since it will be unlocked after putting the receiver in the
       * port's waitqueue.
       */
        waitqueue_prepare_task(&w,owner);
        r=waitqueue_push_intr(&port->waitqueue,&w);

        IPC_UNLOCK_PORT_W(port);
        if( !r ) {
          goto recv_cycle;
        } else {
          goto out;
        }
      } else {
        r = -EWOULDBLOCK;
      }
    } else {
      msg=port->msg_ops->extract_message(port,flags);
      if( msg ) {
        msg->state=MSG_STATE_RECEIVED;

        if( (port->flags & IPC_AUTOREF) && msg->sender ) {
          grab_task_struct(msg->sender);
        }
      }
    }
  }
  IPC_UNLOCK_PORT_W(port);

out:
  if( msg != NULL ) {
    r=__transfer_message_data_to_receiver(msg,iovec,numvec,msg_info,0);

    /* OK, message was successfully transferred, so remove it from the port
     * in case it is a non-blocking transfer.
     */
    IPC_LOCK_PORT_W(port);
    if( !(port->flags & IPC_PORT_SHUTDOWN) &&
        !(port->flags & IPC_BLOCKED_ACCESS) ) {
      port->msg_ops->remove_message(port,msg);
    } 
    IPC_UNLOCK_PORT_W(port);      
    
    if (msg->blocked_mode) {
      POST_MESSAGE_DATA_ACCESS_STEP(port,msg,r,true);
    } else {
      put_ipc_port_message(msg);
    }
  }
  return r;
}

ipc_gen_port_t *ipc_get_port(task_t *task,ulong_t port)
{
  ipc_gen_port_t *p=NULL;
  int r=-EINVAL;
  task_ipc_t *ipc;

  LOCK_TASK_MEMBERS(task);
  ipc=task->ipc;

  if( ipc && ipc->ports ) {
    IPC_LOCK_PORTS(ipc);
    if(port < task->limits->limits[LIMIT_IPC_MAX_PORTS] &&
       ipc->ports[port] != NULL) {
      p = ipc->ports[port];
      if( !(p->flags & IPC_PORT_SHUTDOWN) ) {
        REF_PORT(p);
        r=0;
      }
    }
    IPC_UNLOCK_PORTS(ipc);
  }

  UNLOCK_TASK_MEMBERS(task);
  return p;
}

void ipc_put_port(ipc_gen_port_t *p)
{
  if( atomic_dec_and_test(&p->use_count) ) {
    p->port_ops->destructor(p);
    memfree(p);
  }
}

long ipc_port_send_iov(ipc_channel_t *channel, iovec_t snd_kiovecs[], ulong_t snd_numvecs,
                       iovec_t rcv_kiovecs[], ulong_t rcv_numvecs)
{
  ipc_gen_port_t *port = NULL;
  ipc_port_message_t *msg;
  ipc_buffer_t snd_bufs[MAX_IOVECS], rcv_bufs[MAX_IOVECS];
  ulong_t msg_size = 0, rcv_size = 0;
  long ret = 0;

  ret = __calc_msg_length(snd_kiovecs, snd_numvecs, &msg_size);
  if (ret) {
    return ret;
  }
  if (rcv_kiovecs) {
    ret = __calc_msg_length(rcv_kiovecs, rcv_numvecs, &rcv_size);
    if (ret) {
      return ret;
    }
  }
  
  ret = ipc_get_channel_port(channel, &port);
  if (ret) {
    return -EINVAL;
  }

  msg = ipc_create_port_message_iov_v(channel, snd_kiovecs, snd_numvecs, msg_size,
                                      rcv_kiovecs, rcv_numvecs, snd_bufs, rcv_bufs, rcv_size);
  if (!msg) {
    ret = -ENOMEM;
    goto out;
  }

  ret = ipc_port_send_iov_core(port, msg, channel_in_blocked_mode(channel),
                               rcv_kiovecs, rcv_numvecs, rcv_size);
out:
  if (port) {
    ipc_put_port(port);
  }

  return ret;
}

long ipc_port_send_iov_core(ipc_gen_port_t *port,
                            ipc_port_message_t *msg,bool sync_send,
                            iovec_t *iovecs,ulong_t numvecs,
                            ulong_t reply_len)
{
  ipc_port_msg_ops_t *msg_ops=port->msg_ops;
  size_t msg_size=0;
  long r = 0;
  task_t *sender=current_task();

  if( !msg_ops->insert_message ||
      (sync_send && !msg_ops->dequeue_message) ) {
    return -EINVAL;
  }

  event_set_task(&msg->event,sender);

  IPC_LOCK_PORT_W(port);
  r=(port->flags & IPC_BLOCKED_ACCESS) | sync_send;
  if( r ) {
    /* In case of synchronous message passing both channel and port
     * must be in synchronous mode.
     */
    if( !((port->flags & IPC_BLOCKED_ACCESS) && sync_send) ) {
      r=-EINVAL;
    } else {
      r=0;
    }
  } else {
    msg_size=msg->data_size;
  }

  if( !r ) {
    if( !(port->flags & IPC_PORT_SHUTDOWN ) ) {
      r=msg_ops->insert_message(port,msg);
      waitqueue_pop(&port->waitqueue,NULL);
    } else {
      r=-EPIPE;
    }
  }
  IPC_UNLOCK_PORT_W(port);

  if( r ) {
    return r;
  }

  /* Sender should wait for the reply, so put it into sleep here. */
  if( sync_send ) {
    bool b;
    int ir=0;

    wait_for_reply:
    r=event_yield(&msg->event);
    if( task_was_interrupted(sender) ) {
      /* No luck: we were interrupted asynchronously.
       * So try to remove our message from the port's queue, if possible.
       */
      ir=-EINTR;
      b=true;

      IPC_LOCK_PORT_W(port);
      switch(msg->state) {
        case MSG_STATE_NOT_PROCESSED:
          /* If server neither received nor replied to our message, we can
           * remove it from the queue without any problems.
           */
          msg_ops->remove_message(port,msg);
          break;
        case MSG_STATE_DATA_TRANSFERRED:
          /* Server successfully received data from our message but not replied yet,
           * so we should remove message in port-specific way.
           */
          msg_ops->dequeue_message(port,msg);
          if( port->flags & IPC_AUTOREF ) {
            __release_task_struct(sender);
          }
          break;
        case MSG_STATE_REPLY_BEGIN:
        case MSG_STATE_DATA_UNDER_ACCESS:
        case MSG_STATE_RECEIVED:
          /* Can't remove message right now, must wait for server to change
           * state of the message.
           */
          b=r;
          break;
        case MSG_STATE_REPLIED:
          /* If server finished 'REPLY' phase while we have pending signals,
           * we assume that we weren't actually interrupted by signals.
           * Because server got no errors after performing reply while we have
           * pending signals, reporting error to the client may cause confuses.
           */
          ir=0;
          break; /* Message was replied - do nothing. */
      }
      IPC_UNLOCK_PORT_W(port);

      if( !b ) {
        /* No luck, need to repeat event wait one more time. */
        goto wait_for_reply;
      }
    }

    if( ir ) {
      r=ir;
    } else {
      r=msg->replied_size;
      if( r > 0 ) {
        r=__transfer_reply_data_iov(msg,iovecs,numvecs,false,reply_len,0);
        if( r >= 0 ) {
          r=msg->replied_size;
        }
      }
    }
    put_ipc_port_message(msg);
  } else {
    r=msg_size;
  }
  return r;
}

void ipc_port_remove_poller(ipc_gen_port_t *port,wqueue_task_t *w)
{
  if( w ) {
    IPC_LOCK_PORT_W(port);
    if( w->wq == &port->waitqueue ) {
      waitqueue_delete(w,WQ_DELETE_SIMPLE);
    }
    IPC_UNLOCK_PORT_W(port);
  }
}

/* NOTE: Caller must initialized waitque task before calling this function.
 */
poll_event_t ipc_port_check_events(ipc_gen_port_t *port,wqueue_task_t *w,
                                   poll_event_t evmask)
{
  poll_event_t e=0;

  IPC_LOCK_PORT_W(port);
  if( port->avail_messages ) {
    e=POLLIN | POLLRDNORM;
  }

  e &= evmask;
  if( !e && w ) {
    /* No pending events, so add target task to port's waitqueue. */
    waitqueue_insert(&port->waitqueue,w,WQ_INSERT_SIMPLE);
  }
  IPC_UNLOCK_PORT_W(port);
  return e;
}
