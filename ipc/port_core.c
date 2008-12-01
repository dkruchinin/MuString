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
 * ipc/port.c: implementation of the 'port' IPC abstraction.
 *
 */

#include <eza/arch/types.h>
#include <ipc/port.h>
#include <eza/task.h>
#include <eza/errno.h>
#include <mm/pfalloc.h>
#include <mm/page.h>
#include <ds/linked_array.h>
#include <ipc/ipc.h>
#include <eza/container.h>
#include <ipc/buffer.h>
#include <mlibc/stddef.h>
#include <kernel/vm.h>
#include <eza/arch/preempt.h>
#include <eza/kconsole.h>
#include <mm/slab.h>
#include <ipc/gen_port.h>
#include <eza/event.h>

ipc_port_message_t *ipc_setup_task_port_message(task_t *task,ipc_gen_port_t *p,
                                                uintptr_t snd_buf,ulong_t snd_size,
                                                uintptr_t rcv_buf,ulong_t rcv_size)
{
  ipc_port_message_t *msg = &task->ipc_priv->cached_data.cached_port_message;  
  list_init_node(&msg->l);
  list_init_node(&msg->messages_list);
  IPC_RESET_MESSAGE(msg,task);

  status_t r=-EFAULT;
  task_ipc_priv_t *ipc_priv = task->ipc_priv;

  /* Process send buffer */
  if( snd_size < IPC_BUFFERED_PORT_LENGTH ) {
    msg->send_buffer=ipc_priv->cached_data.cached_page1;
    if( copy_from_user(msg->send_buffer,(void*)snd_buf,snd_size ) ) {
      goto out;
    }
  } else {
    msg->snd_buf.chunks=ipc_priv->cached_data.cached_page1;
    r = ipc_setup_buffer_pages(task,&msg->snd_buf,snd_buf,snd_size);
    if( r ) {
      goto out;
    }
  }

  /* Process receive buffer, if any. */
  if( rcv_size > 0 ) {
    if( rcv_size < IPC_BUFFERED_PORT_LENGTH ) {
      msg->receive_buffer=ipc_priv->cached_data.cached_page2;
    } else {
      msg->rcv_buf.chunks=ipc_priv->cached_data.cached_page2;
      r = ipc_setup_buffer_pages(task,&msg->rcv_buf,rcv_buf,rcv_size);
      if( r ) {
        goto out;
      }
    }
  }

  /* Setup this message. */
  msg->data_size = snd_size;
  msg->reply_size = rcv_size;
  msg->sender=task;
  return msg;
out:
  return NULL;
}

ipc_port_message_t *__ipc_create_nb_port_message(task_t *owner,uintptr_t snd_buf,
                                                 ulong_t snd_size)
{
  if( snd_size <= IPC_NB_MESSAGE_MAXLEN && snd_size ) {
    ipc_port_message_t *msg=memalloc(sizeof(*msg)+snd_size);
    if( msg ) {
      memset(msg,0,sizeof(*msg));
      event_initialize(&msg->event);

      list_init_node(&msg->l);
      list_init_node(&msg->messages_list);

      msg->send_buffer=(void *)((char *)msg+sizeof(*msg));
      msg->data_size=snd_size;
      msg->reply_size=0;
      msg->sender=owner;

      if( !copy_from_user(msg->send_buffer,(void *)snd_buf,snd_size) ) {
        return msg;
      } else {
        memfree(msg);
      }
    }
  }
  return NULL;
}

static void __notify_message_arrived(ipc_gen_port_t *port)
{
    waitqueue_pop(&port->waitqueue, NULL);
}

static status_t __transfer_reply_data(ipc_port_message_t *msg,
                                      uintptr_t reply_buf,ulong_t reply_len,
                                      bool from_server)
{
  status_t r=0;

  reply_len=MIN(reply_len,msg->reply_size);
  if( reply_len > 0 ) {
    if( msg->reply_size <= IPC_BUFFERED_PORT_LENGTH ) {
      /* Short message - copy it from the buffer. */
      if( from_server ) {
        r=copy_from_user(msg->receive_buffer,(void*)reply_buf,reply_len);
      } else {
        r=copy_to_user((void*)reply_buf,msg->receive_buffer,reply_len);
      }
    } else {
      /* Long message - process it via buffer. */
      if( from_server ) {
        r=ipc_transfer_buffer_data(&msg->rcv_buf,0,reply_len,
                                   (void *)reply_buf,from_server);
      } else {
        r=0;
      }
    }
  }

  if( r ) {
    r=-EFAULT;
    reply_len=0;
  }

  /* If we're replying to the message, setup size properly. */
  if( from_server ) {
    msg->replied_size=reply_len;
  }

  return r;
}

status_t __ipc_port_send(struct __ipc_gen_port *port,
                         ipc_port_message_t *msg,ulong_t flags,
                         uintptr_t rcv_buf,ulong_t rcv_size)
{
  ipc_port_msg_ops_t *msg_ops=port->msg_ops;
  status_t r;
  task_t *sender=current_task();

  if( !msg_ops->insert_message ) {
    return -EINVAL;
  }

  /* In case of blocking access both client and server must support
   * this flag.
   */
  r=(port->flags & IPC_BLOCKED_ACCESS) | (flags & IPC_BLOCKED_ACCESS);
  if( r ) {
    r=(port->flags & IPC_BLOCKED_ACCESS) & (flags & IPC_BLOCKED_ACCESS);
    if( !r ) {
      return -EINVAL;
    }
  }

  IPC_LOCK_PORT_W(port);
  if( !(port->flags & IPC_PORT_SHUTDOWN ) ) {
    r=msg_ops->insert_message(port,msg,flags);
  } else {
    r=-EPIPE;
  }
  IPC_UNLOCK_PORT_W(port);

  if( r ) {
    return r;
  }

  __notify_message_arrived(port);

  /* Sender should wait for the reply, so put it into sleep here. */
  if( flags & IPC_BLOCKED_ACCESS ) {
    IPC_TASK_ACCT_OPERATION(sender);
    event_yield( &msg->event );
    IPC_TASK_UNACCT_OPERATION(sender);

    r=msg->replied_size;
    if( r > 0 ) {
      r=__transfer_reply_data(msg,rcv_buf,rcv_size,false);
      if( !r ) {
        r=msg->replied_size;
      }
    }
  }

  return r;
}

static status_t __allocate_port(ipc_gen_port_t **out_port,ulong_t flags,
                                task_t *owner)
{
  ipc_gen_port_t *p = memalloc(sizeof(*p));
  status_t r;

  if( p == NULL ) {
    return -ENOMEM;
  }

  memset(p,0,sizeof(*p));

  /* TODO: [mt] Support flags during IPC port creation. */
  if( flags & IPC_PRIORITIZED_PORT_QUEUE ) {
    return -EINVAL;
  } else {
    p->msg_ops=&def_port_msg_ops;
  }

  p->flags=(flags & IPC_PORT_DIRECT_FLAGS);

  r=p->msg_ops->init_data_storage(p,owner);
  if( r ) {
    goto out_free_port;
  }

  atomic_set(&p->use_count,1);
  atomic_set(&p->own_count,1);
  spinlock_initialize(&p->lock);
  p->avail_messages = p->total_messages=0;
  p->flags = flags;
  list_init_head(&p->channels);
  waitqueue_initialize(&p->waitqueue);
  *out_port = p;
  return 0;
out_free_port:
  memfree(p);
  return r;
}

static void __shutdown_port( ipc_gen_port_t *port )
{
  ipc_port_message_t *msg;

  while((msg=port->msg_ops->remove_head_message(port))) {
    if( event_is_active(&msg->event) ) {
      msg->replied_size=-EPIPE;
      event_raise(&msg->event);
    }
  }
  port->msg_ops->free_data_storage(port);
}

status_t ipc_close_port(task_t *owner,ulong_t port)
{
  task_ipc_t *ipc = get_task_ipc(owner);
  ipc_gen_port_t *p;
  bool shutdown=false;
  status_t r;

  if( !ipc ) {
    return -EINVAL;
  }

  LOCK_IPC(ipc);
  if( !ipc->ports || port >= owner->limits->limits[LIMIT_IPC_MAX_PORTS]) {
    r=-EMFILE;
    goto out_unlock;
  }

  IPC_LOCK_PORTS(ipc);
  p=ipc->ports[port];
  if( p ) {
    IPC_LOCK_PORT_W(p);
    shutdown=atomic_dec_and_test(&p->own_count);
//  kprintf( ">> __ipc_close_port(%p): USE=%d, OWN=%d, SHUTDOWN: %d, IPC: %d\n",
//           p,p->use_count,p->own_count,shutdown,ipc->use_count);
    if(shutdown) {
      p->flags |= IPC_PORT_SHUTDOWN;
      ipc->ports[port]=NULL;
    }
    IPC_UNLOCK_PORT_W(p);
  }
  IPC_UNLOCK_PORTS(ipc);

  if( p ) {
    if( shutdown ) {
      __shutdown_port(p);
      linked_array_free_item(&ipc->ports_array,port);
      ipc->num_ports--;
    }
    r=0;
  } else {
    r=-EINVAL;
  }
out_unlock:
  UNLOCK_IPC(ipc);
  if( p ) {
    __ipc_put_port(p);
  }
  release_task_ipc(ipc);
  return r;
}

status_t __ipc_create_port(task_t *owner,ulong_t flags)
{
  status_t r;
  task_ipc_t *ipc = get_task_ipc(owner);
  ulong_t id;
  ipc_gen_port_t *port;

  if( !ipc ) {
    return -EINVAL;
  }

  LOCK_IPC(ipc);

  if(ipc->num_ports >= owner->limits->limits[LIMIT_IPC_MAX_PORTS]) {
    r = -EMFILE;
    goto out_unlock;
  }

  /* First port created ? */
  if( !ipc->ports ) {
    r = -ENOMEM;
    ipc->ports = alloc_pages_addr(1,AF_PGEN|AF_ZERO);
    if( !ipc->ports ) {
      goto out_unlock;
    }

    if( !linked_array_is_initialized( &ipc->ports_array ) ) {
      if( linked_array_initialize(&ipc->ports_array,
                                  owner->limits->limits[LIMIT_IPC_MAX_PORTS]) != 0 ) {
        /* TODO: [mt] allocate/free memory via slabs. */
        goto out_unlock;
      }
    }
  }

  id = linked_array_alloc_item(&ipc->ports_array);
  if(id == INVALID_ITEM_IDX) {
    r = -EMFILE;
    goto out_unlock;
  }

  /* Ok, it seems that we can create a new port. */
  r = __allocate_port(&port,flags,owner);
  if( r != 0 ) {
    goto free_id;
  }

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
  linked_array_free_item(&ipc->ports_array,id);
out_unlock:
  UNLOCK_IPC(ipc);
  release_task_ipc(ipc);
  return r;
}

/* FIXME: [mt] potential deadlock problem ! [R] */
static void __put_receiver_into_sleep(task_t *receiver,ipc_gen_port_t *port)
{
  wqueue_task_t w;

  IPC_TASK_ACCT_OPERATION(receiver);
  waitqueue_prepare_task(&w,receiver);
  waitqueue_push(&port->waitqueue,&w);
  IPC_TASK_UNACCT_OPERATION(receiver);
}

static status_t __transfer_message_data_to_receiver(ipc_port_message_t *msg,
                                                    ulong_t recv_buf,ulong_t recv_len,
                                                    port_msg_info_t *stats)
{
  status_t r;

  recv_len=MIN(recv_len,msg->data_size);
  if( msg->data_size <= IPC_BUFFERED_PORT_LENGTH ) {
    /* Short message - copy it from the buffer. */
    r=copy_to_user((void *)recv_buf,msg->send_buffer,recv_len);
  } else {
    /* Long message - process it via buffer. */
    r=ipc_transfer_buffer_data(&msg->snd_buf,0,recv_len,
                               (void *)recv_buf,false);
  }

  if( !r ) {
      port_msg_info_t info;

      info.sender_pid=msg->sender->pid;
      info.msg_id=msg->id;
      info.msg_len=recv_len;
      if( copy_to_user(stats,&info,sizeof(info)) ) {
        r=-EFAULT;
      }
  }

  return r;
}

status_t __ipc_port_receive(ipc_gen_port_t *port, ulong_t flags,
                            ulong_t recv_buf,ulong_t recv_len,
                            port_msg_info_t *msg_info)
{
  status_t r=-EINVAL;
  ipc_port_message_t *msg;
  task_t *owner=current_task();
  ipc_port_msg_ops_t *msg_ops=port->msg_ops;
  
  if( !recv_buf || !msg_info || !recv_len ) {
    return -EINVAL;
  }

  /* Main 'Receive' cycle. */
recv_cycle:
  msg=NULL;

  IPC_LOCK_PORT_W(port);
  if( !(port->flags & IPC_PORT_SHUTDOWN) ) {
    if( !port->avail_messages ) {
      /* No incoming messages: sleep (if requested). */
      if( flags & IPC_BLOCKED_ACCESS ) {
      /* No luck: need to sleep. We don't unlock the port right now
       * since in will be unlocked after putting the receiver in the
       * port's waitqueue.
       */
        /* FIXME: [mt] new version of __put_receiver_into_sleep() doesn't
         * require the port to be W-locked !
         */
        IPC_UNLOCK_PORT_W(port);

        __put_receiver_into_sleep(owner,port);
        goto recv_cycle;
      } else {
        r = -EWOULDBLOCK;
      }
    } else {
      /* Got something ! */
      msg=msg_ops->extract_message(port,flags);
    }
  }
  IPC_UNLOCK_PORT_W(port);

  if( msg != NULL ) {
    r=__transfer_message_data_to_receiver(msg,recv_buf,recv_len,msg_info);
    if(r) {
      /* It was impossible to copy message to the buffer, so insert it
       * to the queue again.
       */
      IPC_LOCK_PORT_W(port);
      if( !(port->flags & IPC_PORT_SHUTDOWN) ) {
        msg_ops->requeue_message(port,msg);
      } else {
        r=-EPIPE;
      }
      IPC_UNLOCK_PORT_W(port);
    } else {
      bool free;
      /* OK, message was successfully transferred, so remove it from the port
       * in case it is a non-blocking transfer.
       */
      IPC_LOCK_PORT_W(port);
      if( !(port->flags & IPC_PORT_SHUTDOWN) &&
          !(port->flags & IPC_BLOCKED_ACCESS) ) {
        msg_ops->remove_message(port,msg->id);
        free=true;
      } else {
        free=false;
      }
      IPC_UNLOCK_PORT_W(port);

      if(free) {
        memfree(msg);
      }
    }
  }
  return r;
}

ipc_gen_port_t * __ipc_get_port(task_t *task,ulong_t port)
{
  ipc_gen_port_t *p=NULL;
  status_t r=-EINVAL;
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

void __ipc_put_port(ipc_gen_port_t *p)
{
  UNREF_PORT(p);

  if( !atomic_get(&p->use_count) ) {
//  kprintf( "> FREEING A PORT: %p\n",p );
    memfree(p);
  }
}

status_t __ipc_port_reply(ipc_gen_port_t *port, ulong_t msg_id,
                          ulong_t reply_buf,ulong_t reply_len)
{
  ipc_port_message_t *msg;
  status_t r;

  if( !reply_buf || reply_len > MAX_PORT_MSG_LENGTH ) {
    return -EINVAL;
  }

  if( !port->msg_ops->remove_message ||
      !(port->flags & IPC_BLOCKED_ACCESS)) {
    return -EINVAL;
  }

  msg=NULL;
  IPC_LOCK_PORT_W(port);
  if( !(port->flags & IPC_PORT_SHUTDOWN) ) {
    msg=port->msg_ops->remove_message(port,msg_id);
    if( !msg ) {
      r=-EINVAL;
    }
  } else {
    r=-EPIPE;
  }
  IPC_UNLOCK_PORT_W(port);

  if( msg ) {
    r=__transfer_reply_data(msg,reply_buf,reply_len,true);
    if( event_is_active(&msg->event) ) {
      event_raise(&msg->event);
    }
  }

  return r;
}

poll_event_t ipc_port_get_pending_events(ipc_gen_port_t *port)
{
  poll_event_t e;

  IPC_LOCK_PORT_W(port);
  if(port->avail_messages) {
    e=POLLIN | POLLRDNORM;
  } else {
    e=0;
  }
  IPC_UNLOCK_PORT_W(port);
  return e;
}

void ipc_port_add_poller(ipc_gen_port_t *port,task_t *poller, wqueue_task_t *w)
{
  //IPC_LOCK_PORT_W(port);
  //waitqueue_add_task(&port->waitqueue,w);
  waitqueue_prepare_task(w,poller);
  waitqueue_insert(&port->waitqueue,w,WQ_INSERT_SIMPLE);
  //IPC_UNLOCK_PORT_W(port);
}

void ipc_port_remove_poller(ipc_gen_port_t *port,wqueue_task_t *w)
{
  //IPC_LOCK_PORT_W(port);
  waitqueue_delete(w,WQ_DELETE_SIMPLE);
  //IPC_UNLOCK_PORT_W(port);
}
