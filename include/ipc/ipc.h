#ifndef __IPC_H__
#define  __IPC_H__

#include <eza/arch/types.h>
#include <eza/semaphore.h>
#include <ipc/port.h>
#include <eza/arch/atomic.h>
#include <ds/linked_array.h>
#include <eza/spinlock.h>
#include <eza/arch/arch_ipc.h>
#include <ipc/buffer.h>

/* Blocking mode */
#define IPC_BLOCKED_ACCESS  0x1

/* Send/receive flags. */

/* TODO: [mt] Changes IPC_DEFAULT_PORTS to a smoller value !!! */
#define IPC_DEFAULT_PORTS  512
#define IPC_DEFAULT_PORT_MESSAGES  512
#define IPC_DEFAULT_USER_BUFFERS 512

#define IPC_MAX_PORT_MESSAGES  512

typedef struct __ipc_cached_data {
  void *cached_page1, *cached_page2;
  ipc_port_message_t cached_port_message;
} ipc_cached_data_t;

typedef struct __ipc_pstats {
  atomic_t active_queues; /* Number of waitqueues in the process is. */
} ipc_pstats_t;

typedef struct __task_ipc {
  semaphore_t sem;
  atomic_t use_count;  /* Number of tasks using this IPC structure. */

  /* port-related stuff. */
  ulong_t num_ports;
  ipc_port_t **ports;
  linked_array_t ports_array;
  spinlock_t port_lock;

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

status_t setup_task_ipc(task_t *task);
task_ipc_t *get_task_ipc(task_t *task);
void release_task_ipc(task_ipc_t *ipc);

#define LOCK_IPC(ipc) semaphore_down(&ipc->sem)
#define UNLOCK_IPC(ipc) semaphore_up(&ipc->sem)

#define IPC_LOCK_PORTS(ipc) spinlock_lock(&ipc->port_lock)
#define IPC_UNLOCK_PORTS(ipc) spinlock_unlock(&ipc->port_lock)

#define IPC_LOCK_BUFFERS(ipc) spinlock_lock(&ipc->buffer_lock);
#define IPC_UNLOCK_BUFFERS(ipc) spinlock_unlock(&ipc->buffer_lock);

#define IPC_TASK_ACCT_OPERATION(t) atomic_inc(&t->ipc_priv->pstats.active_queues)
#define IPC_TASK_UNACCT_OPERATION(t) atomic_dec(&t->ipc_priv->pstats.active_queues)

#endif
