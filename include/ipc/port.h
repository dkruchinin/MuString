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
 * include/ipc/port.h: prototypes and data types for the 'port' IPC abstraction.
 *
 */

#ifndef __IPC_PORT__
#define __IPC_PORT__

#include <eza/arch/types.h>
#include <eza/task.h>
#include <eza/spinlock.h>
#include <eza/arch/atomic.h>
#include <ds/linked_array.h>
#include <ds/list.h>
#include <eza/waitqueue.h>
#include <eza/event.h>
#include <ipc/buffer.h>

struct __ipc_port_t;

#define IPC_BUFFERED_PORT_LENGTH  1024

typedef struct __ipc_port_receive_stats {
  ulong_t msg_id,bytes_received;
} ipc_port_receive_stats_t;

typedef struct __ipc_port_messsage_t {
  ulong_t data_size,reply_size,id,replied_size;
  void *send_buffer,*receive_buffer;
  list_node_t l;
  event_t event;
  struct __ipc_port_t *port;
  ipc_user_buffer_t snd_buf, rcv_buf;
  task_t *receiver;  /* To handle 'reply()' properly. */
} ipc_port_message_t;

typedef struct __ipc_port_t {
  ulong_t flags;
  spinlock_t lock;
  linked_array_t msg_array;
  atomic_t use_count,open_count;
  ulong_t queue_size,avail_messages;
  list_head_t messages;
  ipc_port_message_t **message_ptrs;
  task_t *owner;
  wait_queue_t waitqueue;
} ipc_port_t;

#define IPC_LOCK_PORT(p) spinlock_lock(&p->lock)
#define IPC_UNLOCK_PORT(p) spinlock_unlock(&p->lock)

#define IPC_LOCK_PORT_W(p) spinlock_lock(&p->lock)
#define IPC_UNLOCK_PORT_W(p) spinlock_unlock(&p->lock)

void initialize_ipc(void);

status_t ipc_create_port(task_t *owner,ulong_t flags,ulong_t size);
status_t ipc_port_send(task_t *receiver,ulong_t port,uintptr_t snd_buf,
                       ulong_t snd_size,uintptr_t rcv_buf,ulong_t rcv_size);
status_t ipc_port_receive(task_t *owner,ulong_t port,ulong_t flags,
                          ulong_t recv_buf,ulong_t recv_len,
                          ipc_port_receive_stats_t *stats);
status_t ipc_port_reply(task_t *owner, ulong_t port, ulong_t msg_id,
                        ulong_t reply_buf,ulong_t reply_len);

#endif
