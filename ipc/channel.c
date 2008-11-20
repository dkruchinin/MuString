#include <eza/arch/types.h>
#include <eza/task.h>
#include <eza/spinlock.h>
#include <eza/arch/atomic.h>
#include <eza/task.h>
#include <ipc/gen_port.h>
#include <ds/list.h>
#include <ipc/channel.h>
#include <mm/slab.h>
#include <ipc/ipc.h>
#include <ds/linked_array.h>
#include <eza/errno.h>
#include <mm/pfalloc.h>

static ipc_channel_t *__allocate_channel(ipc_gen_port_t *port)
{
  ipc_channel_t *channel=memalloc(sizeof(*channel));

  if( channel ) {
    channel->flags=0;
    spinlock_initialize(&channel->lock);
    channel->server_port=port;
    list_init_node(&channel->ch_list);
    atomic_set(&channel->use_count,1);
  }

  return channel;
}

ipc_channel_t *ipc_get_channel(task_t *task,ulong_t ch_id)
{
  ipc_channel_t *c=NULL;
  task_ipc_t *ipc;

  LOCK_TASK_MEMBERS(task);
  ipc=task->ipc;

  if( ipc && ipc->channels ) {
    IPC_LOCK_CHANNELS(ipc);
    if(ch_id < task->limits->limits[LIMIT_IPC_MAX_CHANNELS] &&
       ipc->channels[ch_id] != NULL) {
      c=ipc->channels[ch_id];
      REF_IPC_ITEM(c);
    }
    IPC_UNLOCK_CHANNELS(ipc);
  }

  UNLOCK_TASK_MEMBERS(task);
  return c;
}

void ipc_put_channel(ipc_channel_t *channel)
{
  UNREF_IPC_ITEM(channel);

  if( !atomic_get(&channel->use_count) ) {
  }
}

void ipc_shutdown_channel(ipc_channel_t *channel)
{
}

status_t ipc_open_channel(task_t *owner,task_t *server,ulong_t port)
{
  task_ipc_t *ipc=get_task_ipc(owner);
  status_t r;
  ulong_t id;
  ipc_channel_t *channel;
  ipc_gen_port_t *server_port;

  if( !ipc ) {
    return -EINVAL;
  }  

  LOCK_IPC(ipc);
  if(ipc->num_channels >= owner->limits->limits[LIMIT_IPC_MAX_CHANNELS]) {
    r = -EMFILE;
    goto out_unlock;
  }

  r=__ipc_get_port(server,port,&server_port);
  if( r ) {
    goto out_unlock;
  }

  /* First channel opened ? */
  if( !ipc->channels ) {
    r = -ENOMEM;
    ipc->channels=alloc_pages_addr(1,AF_PGEN|AF_ZERO);
    if( !ipc->channels ) {
      goto out_put_port;
    }

    if( !linked_array_is_initialized( &ipc->channel_array ) ) {
      if( linked_array_initialize(&ipc->channel_array,
                                  owner->limits->limits[LIMIT_IPC_MAX_CHANNELS]) != 0 ) {
        /* TODO: [mt] allocate/free memory via slabs. */
        goto out_put_port;
      }
    }
  }

  id = linked_array_alloc_item(&ipc->channel_array);
  if(id == INVALID_ITEM_IDX) {
    r=-EMFILE;
    goto out_put_port;
  }

  channel=__allocate_channel(server_port);
  if( !channel ) {
    r=-ENOMEM;
    goto free_id;
  }

  channel->id=id;

  IPC_LOCK_CHANNELS(ipc);
  ipc->channels[id]=channel;
  ipc->num_channels++;
  IPC_UNLOCK_CHANNELS(ipc);

  r=id;
  goto out_unlock;
free_id:
  linked_array_free_item(&ipc->channel_array,id);
out_put_port:
  __ipc_put_port(server_port);
out_unlock:
  UNLOCK_IPC(ipc);
  release_task_ipc(ipc);
  return r;
}
