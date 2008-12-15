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

#ifndef __IPC_GEN_PORT__
#define __IPC_GEN_PORT__

#include <eza/arch/types.h>
#include <eza/task.h>
#include <eza/spinlock.h>
#include <eza/arch/atomic.h>
#include <ds/linked_array.h>
#include <ds/list.h>
#include <ds/waitqueue.h>
#include <eza/event.h>
#include <ipc/buffer.h>
#include <ipc/poll.h>
#include <ipc/port.h>

/****************************************************************************/
#define IPC_PRIORITIZED_PORT_QUEUE   0x20000

#define MAX_PORT_MSG_LENGTH  MB(2)

#define REF_PORT(p)  atomic_inc(&p->use_count)
#define UNREF_PORT(p)  atomic_dec(&p->use_count)

#define IPC_PORT_DIRECT_FLAGS  (IPC_BLOCKED_ACCESS)

struct __ipc_gen_port;

typedef struct __ipc_port_msg_ops {
  status_t (*init_data_storage)(struct __ipc_gen_port *port,task_t *owner);
  status_t (*insert_message)(struct __ipc_gen_port *port,
                             ipc_port_message_t *msg);
  ipc_port_message_t *(*extract_message)(struct __ipc_gen_port *port,
                                         ulong_t flags);
  void (*free_data_storage)(struct __ipc_gen_port *port);
  void (*requeue_message)(struct __ipc_gen_port *port,ipc_port_message_t *msg);
  ipc_port_message_t *(*remove_message)(struct __ipc_gen_port *port,ulong_t msg_id);
  ipc_port_message_t *(*remove_head_message)(struct __ipc_gen_port *port);
} ipc_port_msg_ops_t;

typedef struct __ipc_gen_port {
  ulong_t flags;
  spinlock_t lock;
  atomic_t use_count,own_count;
  ulong_t avail_messages,total_messages;
  wqueue_t waitqueue;

  ipc_port_msg_ops_t *msg_ops;
  void *data_storage;
  list_head_t channels;  
} ipc_gen_port_t;

typedef struct __iovec {
  void *iov_base;
  size_t iov_len;
} iovec_t;

status_t __ipc_port_send(ipc_gen_port_t *port,
                         ipc_port_message_t *msg,bool sync_send,
                         uintptr_t rcv_buf,ulong_t rcv_size);
status_t __ipc_create_port(task_t *owner,ulong_t flags);
status_t __ipc_port_receive(ipc_gen_port_t *port, ulong_t flags,
                            ulong_t recv_buf,ulong_t recv_len,
                            port_msg_info_t *msg_info);
ipc_gen_port_t *__ipc_get_port(task_t *task,ulong_t port);
void __ipc_put_port(ipc_gen_port_t *p);
status_t __ipc_port_reply(ipc_gen_port_t *port, ulong_t msg_id,
                          ulong_t reply_buf,ulong_t reply_len);
status_t ipc_close_port(task_t *owner,ulong_t port);
extern ipc_port_msg_ops_t def_port_msg_ops;
/****************************************************************************/

poll_event_t ipc_port_get_pending_events(ipc_gen_port_t *port);
void ipc_port_add_poller(ipc_gen_port_t *port,task_t *poller, wqueue_task_t *w);
void ipc_port_remove_poller(ipc_gen_port_t *port,wqueue_task_t *w);
ipc_port_message_t *ipc_create_port_message_iov(iovec_t *kiovecs,ulong_t numvecs,
                                                ulong_t data_len,bool blocked,
                                                uintptr_t rcv_buf,ulong_t rcv_size);

#define IPC_NB_MESSAGE_MAXLEN  (512-sizeof(ipc_port_message_t))

#define IPC_RESET_MESSAGE(m,t)   do {           \
    list_init_node(&msg->l);                    \
    list_init_node(&msg->messages_list);        \
    event_initialize(&m->event);                \
    event_set_task(&m->event,t);                \
  } while(0)

#define put_ipc_port_message(m)  memfree((m))

#endif
