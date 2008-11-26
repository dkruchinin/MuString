#include <eza/arch/types.h>
#include <ipc/ipc.h>
#include <mm/pfalloc.h>
#include <mm/page.h>
#include <ds/linked_array.h>
#include <mlibc/string.h>
#include <mm/slab.h>
#include <eza/errno.h>
#include <eza/kconsole.h>

static memcache_t *ipc_priv_data_cache;

void initialize_ipc(void)
{
  ipc_priv_data_cache= create_memcache( "IPC private data memcache",
                                        sizeof(task_ipc_priv_t),1,
                                        SMCF_PGEN);
  if( !ipc_priv_data_cache ) {
    panic( "initialize_ipc(): Can't create the IPC private data memcache !" );
  }
}

static task_ipc_t *__allocate_task_ipc(void)
{
  page_frame_t *p = alloc_page(AF_PGEN | AF_ZERO);
  if( p!= NULL ) {
    task_ipc_t *ipc = (task_ipc_t*)pframe_to_virt(p);

    memset(ipc,0,sizeof(task_ipc_t));

    atomic_set(&ipc->use_count,1);

    spinlock_initialize(&ipc->port_lock);
    mutex_initialize(&ipc->mutex);
    spinlock_initialize(&ipc->buffer_lock);

    return ipc;
  }
  return NULL;
  /* TODO: [mt] allocate task_ipc_t via slabs ! */
}

void free_task_ipc(task_ipc_t *ipc)
{
  /* TODO:[mt] free IPC structure properly ! */
}

static task_ipc_priv_t *__allocate_ipc_private_data(void)
{
  task_ipc_priv_t *priv=alloc_from_memcache(ipc_priv_data_cache);

  if( priv ) {
    memset(&priv->pstats,0,sizeof(ipc_pstats_t));
    atomic_set(&priv->pstats.active_queues,0);
  }
  return priv;
}

static void __free_ipc_private_data(task_ipc_priv_t *p)
{
  if( p->cached_data.cached_page1 ) {
    free_pages_addr(p->cached_data.cached_page1);
  }
  if( p->cached_data.cached_page2 ) {
    free_pages_addr(p->cached_data.cached_page2);
  }
  memfree(p);
}

status_t setup_task_ipc(task_t *task)
{
  task_ipc_t *ipc;
  task_ipc_priv_t *ipc_priv;
  void *p1,*p2;

  if( task->ipc ) {
    ipc=NULL;
  } else {
    ipc=__allocate_task_ipc();
    if( !ipc ) {
      return -ENOMEM;
    }
  }

  ipc_priv=__allocate_ipc_private_data();
  if( !ipc_priv ) {
    goto free_ipc;
  }

  p1=alloc_pages_addr(1, AF_PGEN | AF_ZERO);
  if( !p1 ) {
    goto free_ipc_priv;
  }

  p2=alloc_pages_addr(1, AF_PGEN | AF_ZERO);
  if( !p2 ) {
    goto free_page1;
  }

  ipc_priv->cached_data.cached_page1=p1;
  ipc_priv->cached_data.cached_page2=p2;

  if( !task->ipc ) {
    task->ipc=ipc;
  }
  task->ipc_priv=ipc_priv;
  return 0;
free_page1:
  free_pages_addr(p1);
free_ipc_priv:
  __free_ipc_private_data(ipc_priv);
free_ipc:
  if(ipc) {
    free_task_ipc(ipc);
  }
  return -ENOMEM;
}

void close_ipc_resources(task_ipc_t *ipc)
{
  uint32_t i;

  /* Close all open ports. */
  if( ipc->ports ) {
    for(i=0;i<=ipc->max_port_num;i++) {
      if( ipc->ports[i] ) {
        ipc_close_port(current_task(),i);
      }
    }
  }

  /* Close all channels. */
  if( ipc->channels ) {
    for(i=0;i<=ipc->max_channel_num;i++) {
      if( ipc->channels[i] ) {
        ipc_close_channel(current_task(),i);
      }
    }
  }

  /* Close all buffers */
}

void dup_task_ipc_resources(task_ipc_t *ipc)
{
  int i;

  LOCK_IPC(ipc);

  /* Duplicate all open ports. */
  if( ipc->ports ) {
    for(i=0;i<=ipc->max_port_num;i++) {
      IPC_LOCK_PORTS(ipc);
      if( ipc->ports[i] ) {
        atomic_inc(&ipc->ports[i]->own_count);
      }
      IPC_UNLOCK_PORTS(ipc);
    }
  }

  /* Duplicate all open channels. */
  if( ipc->channels ) {
    for(i=0;i<=ipc->max_channel_num;i++) {
      IPC_LOCK_CHANNELS(ipc);
      if( ipc->channels[i] ) {
        atomic_inc(&ipc->channels[i]->use_count);
      }
      IPC_UNLOCK_CHANNELS(ipc);
    }
  }

  UNLOCK_IPC(ipc);
}

void release_task_ipc_priv(task_ipc_priv_t *priv)
{
  __free_ipc_private_data(priv);
}
