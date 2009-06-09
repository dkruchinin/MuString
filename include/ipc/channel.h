#ifndef __IPC_CHANNEL__
#define __IPC_CHANNEL__

#include <arch/types.h>
#include <mstring/task.h>
#include <sync/spinlock.h>
#include <arch/atomic.h>
#include <ipc/port.h>
#include <ds/list.h>

#define IPC_CHANNEL_FLAG_BLOCKED_MODE  0x1

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
int ipc_open_channel(task_t *owner,task_t *server,ulong_t port, ulong_t flags);
int ipc_open_channel_raw(ipc_gen_port_t *server_port, ulong_t flags, ipc_channel_t **out_channel);
int ipc_close_channel(struct __task_ipc *ipc,ulong_t ch_id);
int ipc_channel_control(task_t *caller,int channel,ulong_t cmd, ulong_t arg);
void ipc_destroy_channel(ipc_channel_t *channel);
ipc_channel_t *ipc_clone_channel(ipc_channel_t *target,struct __task_ipc *newipc);

#define LOCK_CHANNEL(c) spinlock_lock(&c->lock)
#define UNLOCK_CHANNEL(c) spinlock_unlock(&c->lock)

#define kernel_channel(c)  ((c)->ipc == NULL)

static inline int ipc_get_channel_port(ipc_channel_t *c,
                                       ipc_gen_port_t **outport) {
  ipc_gen_port_t *p;
  int r;
  
  LOCK_CHANNEL(c);
  p=c->server_port;
  if( p ) {
    if( !(p->flags & IPC_PORT_SHUTDOWN) ) {
      REF_PORT(p);
      r=0;
    } else {
      p=NULL;
      r=-EPIPE;
    }
  } else {
      r=-EINVAL;
  }
  UNLOCK_CHANNEL(c);

  *outport=p;
  return r;
}

static inline void ipc_pin_channel(ipc_channel_t *channel)
{
  atomic_inc(&channel->use_count);
}

static inline void ipc_unpin_channel(ipc_channel_t *channel)
{
  if (atomic_dec_and_test(&channel->use_count))
    ipc_destroy_channel(channel);
}

static inline void ipc_put_channel(ipc_channel_t *channel)
{
  ipc_unref_channel(channel, 1);
}

#define IPC_CHANNEL_DIRECT_OP_FLAGS  1
#define IPC_CHANNEL_DIRECT_OP_MASK  ((1<<(IPC_CHANNEL_DIRECT_OP_FLAGS))-1)

#define channel_flags_to_op_flags(c) ((c)->flags & IPC_CHANNEL_DIRECT_OP_MASK)
#define channel_in_blocked_mode(c) ((c)->flags & IPC_CHANNEL_FLAG_BLOCKED_MODE)

#define IPC_CHANNEL_CTL_GET_SYNC_MODE  0x1

#endif
