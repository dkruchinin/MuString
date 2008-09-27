#ifndef __IPC_PORT__
#define __IPC_PORT__

#include <eza/arch/types.h>
#include <eza/task.h>
#include <eza/spinlock.h>
#include <eza/arch/atomic.h>
#include <eza/arch/arch_ipc.h>
#include <ds/linked_array.h>

typedef struct __ipc_port_message_t {
  ulong_t mode,data_size;
  arch_ipc_port_ctx_t *ctx;
} ipc_port_message_t;

typedef struct __ipc_port_t {
  ulong_t flags;
  spinlock_t lock;
  linked_array_t msg_list;
  atomic_t use_count;
  ulong_t queue_size,avail_messages;
  ipc_port_message_t **message_ptrs;
} ipc_port_t;


void initialize_ipc(void);
status_t ipc_create_port(task_t *owner,ulong_t flags,ulong_t size);

#endif
