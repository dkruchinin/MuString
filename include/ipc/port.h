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
 * include/ipc/port.h: prototypes and data types for the 'port' IPC abstraction.
 *
 */

#ifndef __IPC_PORT__
#define __IPC_PORT__

#include <eza/arch/types.h>
#include <eza/mutex.h>
#include <eza/task.h>
#include <eza/spinlock.h>
#include <eza/arch/atomic.h>
#include <ds/linked_array.h>
#include <ds/list.h>
#include <eza/waitqueue.h>
#include <eza/event.h>
#include <ipc/buffer.h>
#include <ipc/poll.h>

struct __ipc_port_t;

#define IPC_BUFFERED_PORT_LENGTH  PAGE_SIZE

#define IPC_PORT_SHUTDOWN  0x800

#define INSUFFICIENT_MSG_ID  0x100000
#define WAITQUEUE_MSG_ID     INSUFFICIENT_MSG_ID 

typedef enum __ipc_msg_state {
  MSG_STATE_NOT_PROCESSED,
  MSG_STATE_RECEIVED,
  MSG_STATE_DATA_TRANSFERRED,
  MSG_STATE_REPLY_BEGIN,
  MSG_STATE_REPLIED,
  MSG_STATE_DATA_UNDER_ACCESS,
} ipc_msg_state_t;

typedef struct __ipc_port_message_t {
  ulong_t data_size,reply_size,id;
  long replied_size;
  void *send_buffer,*receive_buffer;
  list_node_t l,messages_list;
  list_head_t h;  /* For implementing skiplists. */
  event_t event;
  struct __ipc_port_t *port;
  ipc_user_buffer_t *snd_buf, *rcv_buf;
  ulong_t num_send_bufs,num_recv_buffers;
  task_t *sender;
  ipc_msg_state_t state;
} ipc_port_message_t;

typedef struct __ipc_port_t {
  ulong_t flags;
  spinlock_t lock;
  linked_array_t msg_array;
  atomic_t use_count;
  ulong_t queue_size,avail_messages;
  list_head_t messages;
  ipc_port_message_t **message_ptrs;
  task_t *owner;
  wqueue_t waitqueue;
} ipc_port_t;

typedef struct __port_msg_info {
  uint16_t msg_id;
  pid_t sender_pid;
  uint32_t msg_len;
  tid_t sender_tid;
  uid_t sender_uid;
  uid_t sender_gid;
} port_msg_info_t;

#define IPC_LOCK_PORT(p) spinlock_lock(&p->lock)
#define IPC_UNLOCK_PORT(p) spinlock_unlock(&p->lock)

#define IPC_LOCK_PORT_W(p) spinlock_lock(&p->lock)
#define IPC_UNLOCK_PORT_W(p) spinlock_unlock(&p->lock)

void initialize_ipc(void);

#endif
