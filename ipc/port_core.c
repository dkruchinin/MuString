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
#include <eza/semaphore.h>
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

ipc_port_message_t *__ipc_create_nb_port_message(task_t *owner,uintptr_t snd_buf,
                                                 ulong_t snd_size)
{
  if( snd_size <= IPC_NB_MESSAGE_MAXLEN && snd_size ) {
    ipc_port_message_t *msg=memalloc(sizeof(*msg)+snd_size);
    if( msg ) {
      memset(msg,0,sizeof(*msg));
      IPC_RESET_MESSAGE(msg,owner);
      msg->send_buffer=(void *)((char *)msg+sizeof(*msg));
      msg->data_size=snd_size;
      msg->reply_size=0;
      msg->sender=owner;

      if( !copy_from_user(msg->send_buffer,snd_buf,snd_size) ) {
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
  waitqueue_wake_one_task(&port->waitqueue);
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

  /* If we're replying to the message, setup size properly. */
  if( from_server ) {
    msg->replied_size=reply_len;
  }

  if( r ) {
    r=-EFAULT;
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
    kprintf( "  * Waiting for reply.\n" );
    IPC_TASK_ACCT_OPERATION(sender);
    event_yield( &msg->event );
    IPC_TASK_UNACCT_OPERATION(sender);
    kprintf( "  * Got a reply: %d\n",msg->replied_size );

    r=msg->replied_size;
    if( r >= 0 ) {
      r=__transfer_reply_data(msg,rcv_buf,rcv_size,false);
      if( !r ) {
        r=msg->replied_size;
      }
    }
  }

  return 0;
}

static status_t __allocate_port(ipc_gen_port_t **out_port,ulong_t flags,
                                task_t *owner)
{
  ipc_gen_port_t *p = memalloc(sizeof(*p));
  status_t r;

  if( p == NULL ) {
    return -ENOMEM;
  }

  /* TODO: [mt] Support flags during IPC port creation. */
  if( flags & IPC_BLOCKED_ACCESS ) {
    if( flags & IPC_PRIORITIZED_PORT_QUEUE ) {
    } else {
      p->msg_ops=&def_port_msg_ops;
    }
  } else {
    p->msg_ops=&nonblock_port_msg_ops;
  }

  r=p->msg_ops->init_data_storage(p,owner);
  if( r ) {
    goto out_free_port;
  }

  atomic_set(&p->use_count,1);
  spinlock_initialize(&p->lock, "");
  p->avail_messages = 0;
  p->flags = flags;
  waitqueue_initialize(&p->waitqueue);
  *out_port = p;
  return 0;
out_free_port:
  memfree(p);
  return r;
}

status_t __ipc_create_port(task_t *owner,ulong_t flags)
{
  status_t r;
  task_ipc_t *ipc = owner->ipc;
  ulong_t id;
  ipc_gen_port_t *port;

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
  IPC_UNLOCK_PORTS(ipc);

  r = id;
  UNLOCK_IPC(ipc);
  return r;
free_id:
  linked_array_free_item(&ipc->ports_array,id);
out_unlock:
  UNLOCK_IPC(ipc);
  return r;
}

/* NOTE: Port must be W-locked before calling this function ! */
static void __put_receiver_into_sleep(task_t *receiver,ipc_gen_port_t *port)
{
  wait_queue_task_t w;

  w.task=receiver;
  waitqueue_add_task(&port->waitqueue,&w);

  /* Now we can unlock the port. */
  IPC_UNLOCK_PORT_W(port);

  IPC_TASK_ACCT_OPERATION(receiver);
  waitqueue_yield(&w);
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

  if( !recv_buf || !msg_info || !recv_len || !owner->ipc ) {
    return -EINVAL;
  }
/*
  if( !valid_user_address((ulong_t)msg_info) ||
      !valid_user_address(recv_buf) ) {
      return -EFAULT;
  }
*/
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
        __put_receiver_into_sleep(owner,port);
        goto recv_cycle;
      } else {
        r = -EWOULDBLOCK;
      }
    } else {
      /* Got something ! */
      msg = port->msg_ops->extract_message(port,flags);
      /* To avoid races between threads that handle the same port. */
      msg->receiver = owner;
    }
  }
  IPC_UNLOCK_PORT_W(port);

  /* We provide a reliable message delivery mechanism, which means
   * that if it was impossible to copy data from a message to the receiver's
   * buffer, we insert the message back to the queue to allow it be processed
   * later.
   */
  if( msg != NULL ) {
    r=__transfer_message_data_to_receiver(msg,recv_buf,recv_len,msg_info);
    if(r) {
      /* It was impossible to copy message to the buffer, so insert it
       * to the queue again.
       */
      IPC_LOCK_PORT_W(port);
      if( !(port->flags & IPC_PORT_SHUTDOWN) ) {
        port->msg_ops->requeue_message(port,msg);
      } else {
        r=-EPIPE;
      }
      IPC_UNLOCK_PORT_W(port);
    }
  }
  return r;
}

status_t __ipc_get_port(task_t *task,ulong_t port,ipc_gen_port_t **out_port)
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
  *out_port = p;
  return r;
}
