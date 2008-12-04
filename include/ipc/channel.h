#ifndef __IPC_CHANNEL__
#define __IPC_CHANNEL__

#include <eza/arch/types.h>
#include <eza/task.h>
#include <eza/spinlock.h>
#include <eza/arch/atomic.h>
#include <eza/task.h>
#include <ipc/gen_port.h>
#include <ds/list.h>

struct __task_ipc;

typedef struct __ipc_channel {
  atomic_t use_count;
  ulong_t flags,id;
  spinlock_t lock;
  ipc_gen_port_t *server_port;
  list_node_t ch_list;
  struct __task_ipc *ipc;
} ipc_channel_t;

ipc_channel_t *ipc_allocate_channel(void);
ipc_channel_t *ipc_get_channel(task_t *task,ulong_t ch_id);
void ipc_unref_channel(ipc_channel_t *channel,ulong_t count);
void ipc_shutdown_channel(ipc_channel_t *channel);
status_t ipc_open_channel(task_t *owner,task_t *server,ulong_t port);
status_t ipc_close_channel(task_t *owner,ulong_t ch_id);

#define LOCK_CHANNEL(c) spinlock_lock(&c->lock)
#define UNLOCK_CHANNEL(c) spinlock_unlock(&c->lock)

#define ipc_put_channel(c)  ipc_unref_channel(c,1)

#endif
