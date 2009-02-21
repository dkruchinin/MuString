#ifndef __IPC_H__
#define  __IPC_H__

#include <eza/arch/types.h>
#include <eza/mutex.h>
#include <ipc/port.h>
#include <eza/arch/atomic.h>
#include <ds/linked_array.h>
#include <eza/spinlock.h>
#include <eza/arch/arch_ipc.h>
#include <ipc/buffer.h>
#include <ipc/channel.h>
#include <ipc/gen_port.h>

/* Blocking mode */
#define IPC_BLOCKED_ACCESS  0x1

#define UNTRUSTED_MANDATORY_FLAGS  (IPC_BLOCKED_ACCESS)

/* NOTE: This number is used for allocate temporary arrays on task's
 * kernel stack. So please don't use huge numbers here.
 */
#define MAX_IOVECS  8

/* TODO: [mt] Changes IPC_DEFAULT_PORTS to a smoller value !!! */
#define IPC_DEFAULT_PORTS  512
#define IPC_DEFAULT_PORT_MESSAGES  512
#define IPC_DEFAULT_USER_BUFFERS  512
#define IPC_DEFAULT_USER_CHANNELS  512

#define IPC_MAX_PORT_MESSAGES  512

typedef struct __ipc_cached_data {
  void *cached_page1, *cached_page2;
  ipc_port_message_t cached_port_message;
} ipc_cached_data_t;

typedef struct __ipc_pstats {
  int foo[4];
} ipc_pstats_t;

typedef struct __task_ipc {
  mutex_t mutex;
  atomic_t use_count;  /* Number of tasks using this IPC structure. */

  /* port-related stuff. */
  ulong_t num_ports,max_port_num;
  ipc_gen_port_t **ports;
  linked_array_t ports_array;
  spinlock_t port_lock;

  /* Channels-related stuff. */
  linked_array_t channel_array;
  ulong_t num_channels,max_channel_num;
  ipc_channel_t **channels;
  spinlock_t channel_lock;

  /* Userspace buffers-related stuff. */
  spinlock_t buffer_lock;
  ipc_user_buffer_t **user_buffers;
  linked_array_t buffers_array;
  ulong_t num_buffers;
} task_ipc_t;

typedef struct __task_ipc_priv {
  /* Cached singletones for synchronous operations. */
  ipc_cached_data_t cached_data;
  ipc_pstats_t pstats;
} task_ipc_priv_t;

int setup_task_ipc(task_t *task);
void release_task_ipc_priv(task_ipc_priv_t *priv);

#define LOCK_IPC(ipc)  mutex_lock(&(ipc)->mutex)
#define UNLOCK_IPC(ipc) mutex_unlock(&(ipc)->mutex);

#define IPC_LOCK_PORTS(ipc) spinlock_lock(&ipc->port_lock)
#define IPC_UNLOCK_PORTS(ipc) spinlock_unlock(&ipc->port_lock)

#define IPC_LOCK_BUFFERS(ipc) spinlock_lock(&ipc->buffer_lock);
#define IPC_UNLOCK_BUFFERS(ipc) spinlock_unlock(&ipc->buffer_lock);

#define IPC_LOCK_CHANNELS(ipc) spinlock_lock(&ipc->channel_lock)
#define IPC_UNLOCK_CHANNELS(ipc) spinlock_unlock(&ipc->channel_lock)

#define REF_IPC_ITEM(c)  atomic_inc(&c->use_count)
#define UNREF_IPC_ITEM(c)  atomic_dec(&c->use_count)

void free_task_ipc(task_ipc_t *ipc);
void close_ipc_resources(task_ipc_t *ipc);
void dup_task_ipc_resources(task_ipc_t *ipc);

static inline task_ipc_t *get_task_ipc(task_t *t)
{
  task_ipc_t *ipc;

  LOCK_TASK_MEMBERS(t);
  if( t->ipc ) {
    atomic_inc(&t->ipc->use_count);
    ipc=t->ipc;
  } else {
    ipc=NULL;
  }
  UNLOCK_TASK_MEMBERS(t);

  return ipc;
}

static inline void release_task_ipc(task_ipc_t *ipc)
{
  atomic_dec(&ipc->use_count);
  if( !atomic_get(&ipc->use_count) ) {
    free_task_ipc(ipc);
  }
}

#endif
