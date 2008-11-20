#ifndef __IPC_CHANNEL__
#define __IPC_CHANNEL__

#include <eza/arch/types.h>
#include <eza/task.h>
#include <eza/spinlock.h>
#include <eza/arch/atomic.h>
#include <eza/task.h>
#include <ipc/gen_port.h>
#include <ds/list.h>

typedef struct __ipc_channel {
  atomic_t use_count;
  ulong_t flags;
  spinlock_t lock;
  ipc_gen_port_t *server_port;
  list_node_t ch_list;
} ipc_channel_t;

ipc_channel_t *ipc_allocate_channel(void);
ipc_channel_t *ipc_get_channel(task_t *owner,ulong_t ch_id);
void ipc_put_channel(ipc_channel_t *channel);
void ipc_shutdown_channel(ipc_channel_t *channel);
status_t ipc_open_channel(task_t *owner,task_t *server,ulong_t port);

#endif
