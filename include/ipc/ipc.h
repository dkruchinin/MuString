#ifndef __IPC_H__
#define  __IPC_H__

#include <eza/arch/types.h>
#include <eza/semaphore.h>
#include <ipc/port.h>
#include <eza/arch/atomic.h>
#include <ds/linked_array.h>

#define IPC_BLOCKED_ACCESSS  0x1

/* TODO: [mt] Changes IPC_DEFAULT_PORTS to a smoller value !!! */
#define IPC_DEFAULT_PORTS  512
#define IPC_DEFAULT_PORT_MESSAGES  512

typedef struct __task_ipc {
  semaphore_t sem;
  atomic_t use_count;
  ulong_t num_ports;
  ipc_port_t **ports;
  linked_array_t ports_array;
} task_ipc_t;

task_ipc_t *allocate_task_ipc(void);

#define LOCK_IPC(t) semaphore_down(&ipc->sem)
#define UNLOCK_IPC(t) semaphore_up(&ipc->sem)

#endif
