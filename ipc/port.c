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

/* NOTE: port must be locked upon calling this function ! */
static bool __port_matches_flags(ipc_port_t *port,ulong_t flags)
{
  return true;
}

static void __put_port(ipc_port_t *p)
{
  atomic_dec(&p->open_count);
  /* TODO: [mt] Free port memory upon deletion. */
}

static status_t __get_port(task_ipc_t *ipc,ulong_t port,ulong_t flags,
                  ipc_port_t **out_port)
{
  ipc_port_t *p;
  status_t r;

  IPC_LOCK_PORTS(ipc);
  if(port >= ipc->num_ports ||
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
  atomic_inc(&p->open_count);
  IPC_UNLOCK_PORTS(ipc);
  *out_port = p;
  return 0;
out_unlock:
  IPC_UNLOCK_PORTS(ipc);
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
  IPC_UNLOCK_PORT_W(port);
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
  task_ipc_t *ipc=receiver->ipc;
  status_t r;
  ipc_port_t *p;
  ipc_port_message_t *msg;
  ulong_t id;
  task_t *sender = current_task();

  if( !ipc ) {
    return -EINVAL;
  }

  /* TODO: [mt] Now only max 112 bytes can be sent vis ports. */
  if( !snd_size || snd_size > 112 || rcv_size > 112 ) {
    return -EINVAL;
  }

  /* First, locate target port. */
  r = __get_port(receiver->ipc,port,flags,&p);
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

status_t ipc_port_receive(task_t *owner,ulong_t port,ulong_t flags)
{
  status_t r;
  task_ipc_t *ipc = owner->ipc;
  ipc_port_t *p;
  ipc_port_message_t *msg;
  
  IPC_LOCK_PORTS(ipc);
  if( port >= ipc->num_ports ) {
    r=-EINVAL;
    goto out_unlock;
  }
  p = ipc->ports[port];
  IPC_UNLOCK_PORTS(ipc);

recv_cycle:
  IPC_LOCK_PORT_W(p);
  if( list_is_empty(&p->messages) ) {
    msg = NULL;
    /* No incoming messages: sleep (if requested). */
    if( flags & IPC_BLOCKED_ACCESS ) {
        /* No luck: need to sleep. We don't unlock the port right now
         * since in will be unlocked after putting the receiver in
         * in the port's waitqueue.
         */
      __put_receiver_into_sleep(owner,p);
      goto recv_cycle;
    } else {
      r = -EAGAIN;
    }
  } else {
    /* Got something ! */
      msg = ___extract_message_from_port_queue(p);
  }
  IPC_UNLOCK_PORT_W(p);

  if(msg != NULL) {
    r = msg->id;
    /* Release this message id to the future messages. */
    kprintf( "(**) ipc_port_receive(): Got a message N%d from %d !\n",
             r,msg->event.task->pid );
  }

  return r;
out_unlock:
  IPC_UNLOCK_PORTS(ipc);
  return r;
}

status_t ipc_port_reply()
{
  return -ENOSYS;
}
