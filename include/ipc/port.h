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

#include <arch/types.h>
#include <sync/mutex.h>
#include <mstring/task.h>
#include <sync/spinlock.h>
#include <arch/atomic.h>
#include <ds/idx_allocator.h>
#include <ds/list.h>
#include <mstring/waitqueue.h>
#include <mstring/event.h>
#include <ipc/buffer.h>
#include <ipc/poll.h>

#define IPC_BUFFERED_PORT_LENGTH  PAGE_SIZE
#define MAX_PORT_MSG_LENGTH  MB2B(2)

/* Common port flags. */
#define IPC_PORT_SHUTDOWN     0x800 /**< Port is under shutdown. */
#define IPC_PORT_ID_INVAL  (~0UL)

#define INSUFFICIENT_MSG_ID  ~(unsigned long)0
#define WAITQUEUE_MSG_ID     INSUFFICIENT_MSG_ID 

#define REF_PORT(p)  atomic_inc(&p->use_count)
#define UNREF_PORT(p)  atomic_dec(&p->use_count)

#define IPC_PORT_DIRECT_FLAGS  (IPC_BLOCKED_ACCESS | IPC_AUTOREF)
#define __MSG_WAS_DEQUEUED  (ipc_port_message_t *)0x007

typedef enum __ipc_msg_state {
  MSG_STATE_NOT_PROCESSED,
  MSG_STATE_RECEIVED,
  MSG_STATE_DATA_TRANSFERRED,
  MSG_STATE_REPLY_BEGIN,
  MSG_STATE_REPLIED,
  MSG_STATE_DATA_UNDER_ACCESS,
} ipc_msg_state_t;

#define IPC_MSG_EXTRA_SIZE  PAGE_SIZE
#define IPC_MSG_EXTRA_PAGES 1

typedef struct __ipc_port_message_t {
  ulong_t data_size,reply_size,id;
  long replied_size;
  void *send_buffer,*receive_buffer;
  list_node_t l,messages_list;
  list_head_t h;  /* For implementing skiplists. */
  event_t event;
  ipc_buffer_t *snd_buf, *rcv_buf;
  ulong_t num_send_bufs,num_recv_buffers;
  task_t *sender;
  ipc_msg_state_t state;
  bool blocked_mode;

  void *extra_data;
  long extra_data_tail;
} ipc_port_message_t;

typedef struct __port_msg_info {
  uint16_t msg_id;  
  pid_t sender_pid;
  uint32_t msg_len;
  tid_t sender_tid;
  uid_t sender_uid;
  uid_t sender_gid;
} port_msg_info_t;

struct __ipc_gen_port;
struct __ipc_channel;

typedef struct __ipc_port_msg_ops {
  ipc_port_message_t *(*lookup_message)(struct __ipc_gen_port *port,
                                        ulong_t msg_id);
  int (*init_data_storage)(struct __ipc_gen_port *port,task_t *owner, ulong_t queue_size);
  int (*insert_message)(struct __ipc_gen_port *port,
                             ipc_port_message_t *msg);
  ipc_port_message_t *(*extract_message)(struct __ipc_gen_port *port,
                                         ulong_t flags);
  void (*dequeue_message)(struct __ipc_gen_port *port,ipc_port_message_t *msg);
  int (*remove_message)(struct __ipc_gen_port *port,
                                    ipc_port_message_t *msg);
  ipc_port_message_t *(*remove_head_message)(struct __ipc_gen_port *port);
} ipc_port_msg_ops_t;

typedef struct __ipc_port_ops {
  void (*destructor)(struct __ipc_gen_port *port);
} ipc_port_ops_t;

typedef struct __ipc_gen_port {
  ulong_t flags;
  spinlock_t lock;
  atomic_t use_count,own_count;
  ulong_t avail_messages,total_messages,capacity;
  wqueue_t waitqueue;
  ipc_port_msg_ops_t *msg_ops;
  ipc_port_ops_t *port_ops;
  void *data_storage;
  list_head_t channels;  
} ipc_gen_port_t;

long ipc_create_port(task_t *owner,ulong_t flags,ulong_t queue_size);
long ipc_port_receive(ipc_gen_port_t *port, ulong_t flags,
                      struct __iovec *iovec, uint32_t numvec,
                      port_msg_info_t *msg_info);
ipc_gen_port_t *ipc_get_port(task_t *task,ulong_t port);
void ipc_put_port(ipc_gen_port_t *p);
int ipc_port_reply(ipc_gen_port_t *port, ulong_t msg_id,
                   ulong_t reply_buf,ulong_t reply_len);
int ipc_close_port(struct __task_ipc *ipc,ulong_t port);

extern ipc_port_msg_ops_t prio_port_msg_ops;
extern ipc_port_ops_t prio_port_ops;

#define IPC_LOCK_PORT(p) spinlock_lock(&p->lock)
#define IPC_UNLOCK_PORT(p) spinlock_unlock(&p->lock)

#define IPC_LOCK_PORT_W(p) IPC_LOCK_PORT((p))
#define IPC_UNLOCK_PORT_W(p) IPC_UNLOCK_PORT((p))

static inline void ipc_lock_ports(ipc_gen_port_t *p1,ipc_gen_port_t *p2) {
  spinlocks_lock2(&p1->lock,&p2->lock);
}

struct __iovec;

poll_event_t ipc_port_check_events(ipc_gen_port_t *port,wqueue_task_t *w,
                                   poll_event_t evmask);
void ipc_port_remove_poller(ipc_gen_port_t *port,wqueue_task_t *w);

void ipc_port_remove_poller(ipc_gen_port_t *port,wqueue_task_t *w);
ipc_port_message_t *ipc_create_port_message_iov_v(struct __ipc_channel *channel, struct __iovec *snd_kiovecs,
                                                  uint32_t snd_numvecs, size_t data_len,
                                                  struct __iovec *rcv_kiovecs, uint32_t rcv_numvecs,
                                                  ipc_buffer_t *snd_bufs, ipc_buffer_t *rcv_bufs, size_t rcv_size);
long ipc_port_send_iov(struct __ipc_channel *channel, struct __iovec *snd_kiovecs, ulong_t snd_numvecs,
                       struct __iovec *rcv_kiovecs, ulong_t rcv_numvecs);
long ipc_port_send_iov_core(ipc_gen_port_t *port,
                            ipc_port_message_t *msg,bool sync_send,
                            struct __iovec *iovecs,ulong_t numvecs,
                            ulong_t reply_len);
long ipc_port_msg_read(struct __ipc_gen_port *port,ulong_t msg_id,
                       struct __iovec *rcv_iov,ulong_t numvecs,ulong_t offset);
long ipc_port_msg_write(struct __ipc_gen_port *port,ulong_t msg_id,
                        struct __iovec *iovecs,ulong_t numvecs,off_t *offset,
                        long len, bool wakeup,long msg_size);

ipc_gen_port_t *ipc_clone_port(ipc_gen_port_t *p);
void put_ipc_port_message(ipc_port_message_t *msg);

#define IPC_NB_MESSAGE_MAXLEN  (512-sizeof(ipc_port_message_t))

#define IPC_RESET_MESSAGE(m,t)                  \
    do {                                        \
      memset(m, 0, sizeof(*(m)));               \
      list_init_node(&(m)->l);                  \
      list_init_node(&(m)->messages_list);      \
      event_initialize(&(m)->event);            \
      event_set_task(&(m)->event,(t));          \
      (m)->state=MSG_STATE_NOT_PROCESSED;       \
  } while(0)

#define ipc_message_data(m) ((m)->data_size <= IPC_NB_MESSAGE_MAXLEN ? (void *)(m)->send_buffer : NULL )

long ipc_port_control(ipc_gen_port_t *p, ulong_t cmd, ulong_t arg);

#define message_writable(msg)  ((msg)->state != MSG_STATE_REPLY_BEGIN && (msg)->state != MSG_STATE_DATA_UNDER_ACCESS )
#define mark_message_waccess(msg) do { (msg)->state = MSG_STATE_DATA_UNDER_ACCESS; } while(0)
#define unmark_message_waccess(msg) do { (msg)->state = MSG_STATE_DATA_TRANSFERRED; } while(0)

/* NOTE: port must be locked ! */
static inline ipc_port_message_t *__ipc_port_lookup_message(ipc_gen_port_t *p,
                                                            ulong_t msg_id,
                                                            long *err)
{
  ipc_port_message_t *msg;
  long e;

  if( p->flags & IPC_PORT_SHUTDOWN ) {
    e=-EPIPE;
    goto out;
  }

  msg = p->msg_ops->lookup_message(p,msg_id);
  if( msg == __MSG_WAS_DEQUEUED ) { /* Client got lost somehow. */
    e=-ENXIO;
  } else if( !msg ) {
    e=-EINVAL;
  } else {
    e=0;
  }

out:
  *err = e;
  if( e ) {
    return NULL;
  } else {
    return msg;
  }
}

#endif
