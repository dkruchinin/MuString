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

static void __put_port(ipc_port_t *port)
{
  atomic_dec(&port->use_count);
}

static ipc_port_message_t *__allocate_port_message(ipc_port_t *port)
{
  ipc_port_message_t *m = alloc_pages_addr(1,AF_PGEN);
  list_init_node(&m->l);
  m->retcode = 0;
  m->port = port;
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

static status_t __put_sender_into_sleep(task_t *sender,ipc_port_t *port,
                                        ipc_port_message_t *msg)
{
  return 0;
}

static status_t __put_receiver_into_sleep(task_t *sender,ipc_port_t *port)
{
  return 0;
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
    linked_array_free_item(&ipc->ports_array,id);
    goto out_unlock;
  }

  /* Install new port. */
  IPC_LOCK_PORTS(ipc);
  ipc->ports[id] = port;
  ipc->num_ports++;
  IPC_UNLOCK_PORTS(ipc);

  r = id;
  kprintf( "++ PORT ADDRESS: %p\n", port );
out_unlock:
  UNLOCK_IPC(ipc);
  return r;
}

status_t ipc_open_port(task_t *owner,ulong_t port,ulong_t flags,
                       task_t *opener)
{
  task_ipc_t *ipc=opener->ipc;
  status_t r;
  ipc_port_t *p;
  ulong_t id;

  /* Don't deal with daemons ! */
  if( !owner->ipc || !opener->ipc ) {
    return -EINVAL;
  }

  LOCK_IPC(ipc);
  if(ipc->num_open_ports >= opener->limits->limits[LIMIT_IPC_MAX_OPEN_PORTS]) {
    r = -EMFILE;
    goto out_unlock;
  }

  /* First, locate target port. */
  r = __get_port(owner->ipc,port,flags,&p);
  if( r ) {
    goto out_unlock;
  }

  /* Now we can install this port in our descriptor table. */
  r = -ENOMEM;

  /* First port opened ? */
  if( !ipc->open_ports ) {
    ipc->open_ports = alloc_pages_addr(1,AF_PGEN|AF_ZERO);
    if( !ipc->open_ports ) {
      goto out_put_port;
    }
  }

  /* First port opened ? */
  if( !linked_array_is_initialized(&ipc->open_ports_array) ) {
    if( linked_array_initialize(&ipc->open_ports_array,
                                  opener->limits->limits[LIMIT_IPC_MAX_OPEN_PORTS]) != 0 ) {
        /* TODO: [mt] allocate/free memory via slabs. */
        goto out_put_port;
      }
  }

  /* Get free descriptor. */
  id = linked_array_alloc_item(&ipc->open_ports_array);
  if(id == INVALID_ITEM_IDX) {
    r = -EMFILE;
    goto out_put_port;
  }

  /* Install this opened port. */
  IPC_LOCK_OPEN_PORTS(ipc);
  ipc->open_ports[id]=p;
  ipc->num_open_ports++;
  IPC_UNLOCK_OPEN_PORTS(ipc);

  UNLOCK_IPC(ipc);
  return id;
out_put_port:
  __put_port(p);
out_unlock:
  UNLOCK_IPC(ipc);
  return r;
}

status_t ipc_port_send(task_t *sender,ulong_t port,ulong_t snd_size,
                       ulong_t rcv_size,ulong_t flags)
{
  task_ipc_t *ipc=sender->ipc;
  status_t r;
  ipc_port_t *p;
  ipc_port_message_t *msg;
  ulong_t id;

  if( !ipc ) {
    return -EINVAL;
  }

  /* TODO: [mt] Now only max 112 bytes can be sent vis ports. */
  if( snd_size > 112 || rcv_size > 112 ) {
    return -EINVAL;
  }

  if( !snd_size || port >= sender->ipc->num_open_ports ) {
    return -EINVAL;
  }

  IPC_LOCK_OPEN_PORTS(ipc);
  p = ipc->open_ports[port];
  IPC_UNLOCK_OPEN_PORTS(ipc);

  if( !p ) {
    return -EINVAL;
  }

  if( p->avail_messages == p->queue_size ) {
    return -EBUSY;
  }

  msg = __allocate_port_message(p);
  if(msg==NULL) {
    return -ENOMEM;
  }

  id = __allocate_port_message_id(p);
  if( id==INVALID_ITEM_IDX ) {
    r = -EBUSY;
    goto free_message;
  }

  /* Setup this message. */
  msg->sender = sender;
  msg->data_size = snd_size;
  msg->reply_size = rcv_size;
  msg->flags = flags;
  /* Setup message data in arch-specific manner. */
  r = arch_setup_port_message_buffers(sender,msg);

  if( r != 0 ) {
    goto free_message_id;
  }

  /* OK, new message is ready for sending. */
  __add_message_to_port_queue(p,msg,id);
  __notify_message_arrived(p);

  if( p->flags & IPC_BLOCKED_ACCESS ||
      rcv_size != 0 ) {
    /* Sender should wait for the reply, so put it into sleep here. */
    __put_sender_into_sleep(sender,p,msg);
  }

  return msg->retcode;
free_message_id:
  __free_port_message_id(p,id);
free_message:
  __free_port_message(msg);
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
      /* No luck: need to sleep. */
      IPC_UNLOCK_PORT_W(p);
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
             r,msg->sender->pid );
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
