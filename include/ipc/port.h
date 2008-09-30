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
#include <eza/arch/arch_ipc.h>
#include <ds/linked_array.h>
#include <ds/list.h>

typedef struct __ipc_port_messsage_t {
  ulong_t data_size,reply_size,id;
  task_t *sender;
  arch_ipc_port_ctx_t *ctx;
  list_node_t l;
  status_t retcode;
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
} ipc_port_t;

#define IPC_LOCK_PORT(p) spinlock_lock(&p->lock)
#define IPC_UNLOCK_PORT(p) spinlock_unlock(&p->lock)

#define IPC_LOCK_PORT_W(p) spinlock_lock(&p->lock)
#define IPC_UNLOCK_PORT_W(p) spinlock_unlock(&p->lock)

void initialize_ipc(void);
status_t ipc_create_port(task_t *owner,ulong_t flags,ulong_t size);
status_t ipc_open_port(task_t *owner,ulong_t port,ulong_t flags,
                       task_t *opener);

arch_ipc_port_ctx_t *arch_ipc_get_sender_port_ctx(task_t *caller);
arch_ipc_port_ctx_t *arch_ipc_get_receiver_port_ctx(task_t *caller);

#endif
