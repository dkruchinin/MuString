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

#define MAX_PORT_MSG_LENGTH  64

#define REF_PORT(p)  atomic_inc(&p->open_count)
#define UNREF_PORT(p)  atomic_dec(&p->open_count)

/* NOTE: port must be locked upon calling this function ! */
static bool __port_matches_flags(ipc_port_t *port,ulong_t flags)
{
  return true;
}

static void __put_port(ipc_port_t *p)
{
  UNREF_PORT(p);
  /* TODO: [mt] Free port memory upon deletion. */
}


static status_t __get_port(task_t *task,ulong_t port,ulong_t flags,
                  ipc_port_t **out_port)
{
  ipc_port_t *p;
  status_t r;
  task_ipc_t *ipc = task->ipc;

  IPC_LOCK_PORTS(ipc);
  if(port >= task->limits->limits[LIMIT_IPC_MAX_PORTS] ||
     ipc->ports[port] == NULL) {
    r=-EINVAL;
    goto out_unlock;
  }
  p = ipc->ports[port];

  /* Check access mode, if any */
  if( flags != 0 && !__port_matches_flags(p,flags) ) {
    r=-EPERM;
    goto out_unlock;
  }
  REF_PORT(p);
  IPC_UNLOCK_PORTS(ipc);
  *out_port = p;
  return 0;
out_unlock:
  IPC_UNLOCK_PORTS(ipc);
  *out_port = NULL;
  return r;
}

static ipc_port_message_t *__allocate_port_message(ipc_port_t *port)
{
  ipc_port_message_t *m = alloc_pages_addr(1,AF_PGEN);
  list_init_node(&m->l);
  m->retcode = 0;
  m->port = port;
  event_initialize(&m->event);
  return m;
}

/* NOTE: port must be locked before calling this function ! */
static void ___free_port_message_id(ipc_port_t *port,ulong_t id)
{
  linked_array_free_item(&port->msg_array,id);
}

static ulong_t __allocate_port_message_id(ipc_port_t *port)
{
  ulong_t id;

  IPC_LOCK_PORT_W(port);
  id = linked_array_alloc_item(&port->msg_array);
  IPC_UNLOCK_PORT_W(port);
  return id;
}

static void __free_port_message(ipc_port_message_t *msg)
{
  free_pages_addr(msg);
}

static void __notify_message_arrived(ipc_port_t *port)
{
  kprintf( "++ Notifying the port owner: %d\n", port->owner->pid );
  waitqueue_wake_one_task(&port->waitqueue);
}

static void __add_message_to_port_queue( ipc_port_t *port,
                                         ipc_port_message_t *msg,
                                         ulong_t id)
{
  IPC_LOCK_PORT_W(port);
  list_add2tail(&port->messages,&msg->l);
  port->message_ptrs[id] = msg;
  port->avail_messages++;
  msg->id = id;
  IPC_UNLOCK_PORT_W(port);
}

static void __free_port_message_id(ipc_port_t *port,ulong_t id)
{
  IPC_LOCK_PORT_W(port);
  ___free_port_message_id(port,id);
  port->message_ptrs[id] = NULL;
  IPC_UNLOCK_PORT_W(port);
}

static status_t __copy_message_data_to_receiver(task_t *receiver,
                                                ipc_port_message_t *msg)
{
  /* TODO: [mt] Support all port types, not only 'pulses'. */
  
  return 0;
}

/* NOTE: port must be locked before calling this function ! */
static ipc_port_message_t *___extract_message_from_port_queue(ipc_port_t *p)
{
  ipc_port_message_t *msg;
  list_node_t *lm = list_node_first(&p->messages);

  msg = container_of(list_node_first(&p->messages),ipc_port_message_t,l);
  list_del(lm);
  p->avail_messages--;
  return msg;
}

/* NOTE: Port must be W-locked before calling this function ! */
static void __put_receiver_into_sleep(task_t *receiver,ipc_port_t *port)
{
  wait_queue_task_t w;

  w.task=receiver;
  waitqueue_add_task(&port->waitqueue,&w);

  /* Now we can unlock the port. */
  IPC_UNLOCK_PORT_W(port);

  kprintf( "[port]: putting receiver %d into sleep.\n",receiver->pid );
  waitqueue_yield(&w);
}

static status_t __allocate_port(ipc_port_t **out_port,ulong_t flags,
                                ulong_t queue_size, task_t *owner)
{
  ipc_port_t *p = alloc_pages_addr(1,AF_PGEN|AF_ZERO);
  if( p == NULL ) {
    return -ENOMEM;
  }

  /* TODO: [mt] Allocate array of proper size for port messages */
  p->message_ptrs = alloc_pages_addr(1,AF_PGEN|AF_ZERO);
  if( p->message_ptrs == NULL ) {
    return -ENOMEM;
  }

  if( !(flags & IPC_BLOCKED_ACCESS) ) {
    kprintf( KO_WARNING "Non-blocking ports aren't implemented yet !\n" );
    return -EINVAL;
  }

  atomic_set(&p->use_count,1);
  atomic_set(&p->open_count,0);
  spinlock_initialize(&p->lock, "");
  /* TODO: [mt] allocate memory via slabs ! */
  linked_array_initialize(&p->msg_array,
                          owner->limits->limits[LIMIT_IPC_MAX_PORT_MESSAGES] );
  p->queue_size = queue_size;
  p->avail_messages = 0;
  p->flags = flags;
  p->owner = owner;

  list_init_head(&p->messages);
  waitqueue_initialize(&p->waitqueue);
  *out_port = p;
  return 0;
}

status_t ipc_create_port(task_t *owner,ulong_t flags,ulong_t size)
{
  status_t r;
  task_ipc_t *ipc = owner->ipc;
  ulong_t id;
  ipc_port_t *port;

  LOCK_IPC(ipc);

  if(size > owner->limits->limits[LIMIT_IPC_MAX_PORT_MESSAGES] ) {
    r = -E2BIG;
    goto out_unlock;
  }

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
  r = __allocate_port(&port,flags,size,owner);
  if( r != 0 ) {
    goto free_id;
  }

  /* Install new port. */
  IPC_LOCK_PORTS(ipc)
  ipc->ports[id] = port;
  ipc->num_ports++;
  IPC_UNLOCK_PORTS(ipc);

  r = id;
  UNLOCK_IPC(ipc);
  kprintf( "++ PORT ADDRESS: %p\n", port );
  return r;
free_id:
  linked_array_free_item(&ipc->ports_array,id);
out_unlock:
  UNLOCK_IPC(ipc);
  return r;
}


status_t ipc_port_send(task_t *receiver,ulong_t port,ulong_t snd_size,
                       ulong_t rcv_size,ulong_t flags)
{
  status_t r;
  ipc_port_t *p;
  ipc_port_message_t *msg;
  ulong_t id;
  task_t *sender = current_task();

  if( !receiver->ipc ) {
    return -EINVAL;
  }

  /* TODO: [mt] Now only max ~64 vbytes can be sent vis ports. */
  if( !snd_size || snd_size > MAX_PORT_MSG_LENGTH ||
      rcv_size > MAX_PORT_MSG_LENGTH ) {
    return -EINVAL;
  }

  /* First, locate target port. */
  r = __get_port(receiver,port,flags,&p);
  if( r ) {
    return -EINVAL;
  }

  /* First check without locking the port. */
  if( p->avail_messages == p->queue_size ) {
    r = -EBUSY;
    goto put_port;
  }

  msg = __allocate_port_message(p);
  if( msg==NULL ) {
    r = -ENOMEM;
    goto put_port;
  }

  id = __allocate_port_message_id(p);
  if( id==INVALID_ITEM_IDX ) {
    r = -EBUSY;
    goto free_message;
  }

  /* Setup this message. */
  msg->data_size = snd_size;
  msg->reply_size = rcv_size;
  msg->flags = flags;

  /* TODO: [mt] implement all port message types. */
  msg->type = PORT_MESSAGE_PULSE;

  /* Setup message data in arch-specific manner. */
  r = arch_setup_port_message_buffers(sender,msg);

  if( r != 0 ) {
    goto free_message_id;
  }

  /* Finally, setup event. */
  event_set_task(&msg->event,sender);

  /* OK, new message is ready for sending. */
  __add_message_to_port_queue(p,msg,id);
  __notify_message_arrived(p);

  if( p->flags & IPC_BLOCKED_ACCESS ||
      rcv_size != 0 ) {
    /* Sender should wait for the reply, so put it into sleep here. */
    kprintf( "[port]: putting sender %d into sleep.\n",sender->pid );
    event_yield( &msg->event );
  }

  kprintf( "[port] Sender returned from sleep.\n" );
  /* Release the port. */
  __put_port(p);
  return msg->retcode;
free_message_id:
  __free_port_message_id(p,id);
free_message:
  __free_port_message(msg);
put_port:
  __put_port(p);
  return r;
}

static ipc_port_t *__get_port_for_server(task_t *server,ulong_t port)
{
  task_ipc_t *ipc = server->ipc;
  ipc_port_t *p;
  ulong_t maxport = server->limits->limits[LIMIT_IPC_MAX_PORTS];

  IPC_LOCK_PORTS(ipc);
  if( port >= maxport ) {
    p = NULL;
  } else {
    p = ipc->ports[port];
    if( p != NULL ) {
      REF_PORT(p);
    }
  }
  IPC_UNLOCK_PORTS(ipc);
  return p;
}

status_t ipc_port_receive(task_t *owner,ulong_t port,ulong_t flags,
                          ulong_t recv_buf,ulong_t recv_len)
{
  status_t r;
  ipc_port_t *p;
  ipc_port_message_t *msg;

  p = __get_port_for_server(owner,port);
  if( p == NULL ) {
    return -EINVAL;
  }

  /* Main 'Receive' cycle. */
recv_cycle:
  IPC_LOCK_PORT_W(p);
  if( list_is_empty(&p->messages) ) {
    msg = NULL;
    /* No incoming messages: sleep (if requested). */
    if( flags & IPC_BLOCKED_ACCESS ) {
      /* No luck: need to sleep. We don't unlock the port right now
       * since in will be unlocked after putting the receiver in the
       * port's waitqueue.
     */
      __put_receiver_into_sleep(owner,p);
      goto recv_cycle;
    } else {
      r = -EWOULDBLOCK;
    }
  } else {
    /* Got something ! */
    msg = ___extract_message_from_port_queue(p);
    /* To avoid races between threads that handle the same port. */
    msg->receiver = owner;
  }
  IPC_UNLOCK_PORT_W(p);

  /* We provide a reliable message delivery mechanism, which means
   * that if it was impossible to copy data from a message to the receiver's
   * buffer, we insert the message back to the queue to allow it be processed
   * later.
   */
  if( msg != NULL ) {
    r=arch_copy_port_message_to_receiver(owner,msg);
    if( !r ) {
      r = msg->id;
    } else {
      /* It was impossible to copy message to the buffer, so insert it
       * to the queue again.
       */
      __add_message_to_port_queue(p,msg,msg->id);
    }
  }

  /* Release the port. */
  __put_port(p);
  return r;
}

status_t ipc_port_reply(task_t *owner, ulong_t port, ulong_t msg_id,
                        ulong_t reply_buf,ulong_t reply_len)
{
  status_t r;
  ipc_port_t *p;
  ipc_port_message_t *msg;

  p = __get_port_for_server(owner,port);
  if( p == NULL ) {
    return -EINVAL;
  }

  IPC_LOCK_PORT(p);
  if( msg_id < p->queue_size ) {
    msg = p->message_ptrs[msg_id];
  } else {
    msg = NULL;
  }
  IPC_UNLOCK_PORT(p);

  if( msg == NULL ) {
    r=-EINVAL;
    goto out;
  }

  if( msg->receiver != owner ) {
    r=-EINVAL;
    goto out;
  }

  /* TODO: [mt] copy data to sender's buffer. */

  /* Free this message so its ID will be used yet again. */
  __free_port_message_id(p,msg->id);

  /* Ok, we can reply to this message. */
  kprintf( "[++] Replying back to %d\n", msg->event.task->pid );
  event_raise(&msg->event);

  r = 0;
out:
  __put_port(p);
  return r;
}
