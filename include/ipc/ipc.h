#ifndef __IPC_H__
#define  __IPC_H__

#include <eza/arch/types.h>
#include <eza/semaphore.h>
#include <ipc/port.h>
#include <eza/arch/atomic.h>
#include <ds/linked_array.h>
#include <eza/spinlock.h>
#include <eza/arch/arch_ipc.h>

/* Blocking mode */
#define IPC_BLOCKED_ACCESS  0x1

/* Send/receive flags. */

/* TODO: [mt] Changes IPC_DEFAULT_PORTS to a smoller value !!! */
#define IPC_DEFAULT_PORTS  512
#define IPC_DEFAULT_PORT_MESSAGES  512
#define IPC_DEFAULT_OPEN_PORTS  512

typedef struct __task_ipc {
  semaphore_t sem;
  atomic_t use_count;

  /* port-related stuff. */
  ulong_t num_ports;
  ipc_port_t **ports;
  linked_array_t ports_array;
  spinlock_t port_lock;

  /* open IPCs are stored here. */
  ulong_t num_open_ports;
  ipc_port_t **open_ports;
  linked_array_t open_ports_array;
  spinlock_t open_port_lock;

  /* Singletones for blocking operations. */
  arch_ipc_port_ctx_t *task_port_ctx;
} task_ipc_t;

task_ipc_t *allocate_task_ipc(void);

#define LOCK_IPC(ipc) semaphore_down(&ipc->sem)
#define UNLOCK_IPC(ipc) semaphore_up(&ipc->sem)

#define IPC_LOCK_PORTS(ipc) spinlock_lock(&ipc->port_lock)
#define IPC_UNLOCK_PORTS(ipc) spinlock_unlock(&ipc->port_lock)

#define IPC_LOCK_OPEN_PORTS(ipc) spinlock_lock(&ipc->open_port_lock)
#define IPC_UNLOCK_OPEN_PORTS(ipc) spinlock_unlock(&ipc->open_port_lock)

#endif
