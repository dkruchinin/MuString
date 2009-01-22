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
#include <eza/security.h>
#include <ipc/ipc.h>

static ipc_channel_t *__allocate_channel(ipc_gen_port_t *port,ulong_t flags)
{
  ipc_channel_t *channel=memalloc(sizeof(*channel));

  if( channel ) {
    channel->flags=flags;
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
  task_ipc_t *ipc=get_task_ipc(task);

  if( ipc ) {
    if( ipc->channels ) {
      IPC_LOCK_CHANNELS(ipc);
      if(ch_id < task->limits->limits[LIMIT_IPC_MAX_CHANNELS] &&
         ipc->channels[ch_id] != NULL) {
        c=ipc->channels[ch_id];
        atomic_inc(&c->use_count);
      }
      IPC_UNLOCK_CHANNELS(ipc);
    }
    release_task_ipc(ipc);
  }

  return c;
}

static void __shutdown_channel(ipc_channel_t *channel)
{
  __ipc_put_port(channel->server_port);
  memfree(channel);
}

void ipc_unref_channel(ipc_channel_t *channel,ulong_t c)
{
  bool shutdown;
  task_ipc_t *ipc=channel->ipc;

  IPC_LOCK_CHANNELS(channel->ipc);
  if(ipc->channels) {
    if( atomic_sub_and_test(&channel->use_count,c) ) {
      shutdown=true;
      ipc->channels[channel->id]=NULL;
      ipc->num_channels--;
      linked_array_free_item(&ipc->channel_array,channel->id);
    } else {
      shutdown=false;
    }
  }
  IPC_UNLOCK_CHANNELS(channel->ipc);

  if( shutdown ) {
//    kprintf( "* Shutting down channel: %d\n",channel->id );
    __shutdown_channel(channel);
  }
}

int ipc_close_channel(task_t *owner,ulong_t ch_id)
{
  task_ipc_t *ipc=get_task_ipc(owner);
  ipc_channel_t *c;
  int r;

  if( !ipc ) {
    return -EINVAL;
  }

  LOCK_IPC(ipc);
  c=ipc_get_channel(current_task(),ch_id);

  if( !c ) {
    r=-EINVAL;
  } else {
    /* One reference after 'ipc_get_channel()' + one initial reference. */
    ipc_unref_channel(c,2);
    r=0;
  }

  UNLOCK_IPC(ipc);
  release_task_ipc(ipc);
  return r;
}

int __check_port_flags( ipc_gen_port_t *port,ulong_t flags)
{
  int i,r=0;

  IPC_LOCK_PORT_W(port);
  /* 1: Check blocking access. */
  i=(port->flags & IPC_BLOCKED_ACCESS) | (flags & IPC_CHANNEL_FLAG_BLOCKED_MODE);
  if( i ) {
    if( !((port->flags & IPC_BLOCKED_ACCESS) && (flags & IPC_CHANNEL_FLAG_BLOCKED_MODE)) ) {
      r=-EINVAL;
    }
  }
  IPC_UNLOCK_PORT_W(port);
  return r;
}

int ipc_open_channel(task_t *owner,task_t *server,ulong_t port,
                          ulong_t flags)
{
  task_ipc_t *ipc=get_task_ipc(owner);
  int r;
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

  server_port=__ipc_get_port(server,port);
  if( !server_port ) {
    r=-EINVAL;
    goto out_unlock;
  }

  if( __check_port_flags(server_port,flags) ) {
    r=-EINVAL;
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

  channel=__allocate_channel(server_port,flags);
  if( !channel ) {
    r=-ENOMEM;
    goto free_id;
  }

  channel->id=id;
  channel->ipc=ipc;

  IPC_LOCK_CHANNELS(ipc);
  ipc->channels[id]=channel;
  ipc->num_channels++;

  if( id > ipc->max_channel_num ) {
    ipc->max_channel_num=id;
  }
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

int ipc_channel_control(task_t *caller,int channel,ulong_t cmd,ulong_t arg) {
  ipc_channel_t *c=ipc_get_channel(caller,channel);
  int r;

  if( !c ) {
    return -EINVAL;
  }

  switch( cmd ) {
    case IPC_CHANNEL_CTL_GET_SYNC_MODE:
      r= c->flags & IPC_CHANNEL_FLAG_BLOCKED_MODE ? 1 : 0;
      break;
    default:
      r=-EINVAL;
      break;
  }

put_channel:
  ipc_put_channel(c);
  return r;
}
