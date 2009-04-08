#include <eza/arch/types.h>
#include <ipc/ipc.h>
#include <mm/pfalloc.h>
#include <mm/page.h>
#include <ds/idx_allocator.h>
#include <mlibc/string.h>
#include <mm/slab.h>
#include <eza/errno.h>
#include <eza/kconsole.h>
#include <mlibc/string.h>
#include <config.h>

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


static void __close_ipc_resources(task_ipc_t *ipc)
{
  uint32_t i;

  /* Close all open ports. */
  if( ipc->ports ) {
    for(i=0;i<=ipc->max_port_num;i++) {
      if( ipc->ports[i] ) {
        ipc_close_port(ipc,i);
      }
    }
  }

  /* Close all channels. */
  if( ipc->channels ) {
    for(i=0;i<=ipc->max_channel_num;i++) {
      if( ipc->channels[i] ) {
        ipc_close_channel(ipc,i);
      }
    }
  }
}

void release_task_ipc(task_ipc_t *ipc)
{
  if( atomic_dec_and_test(&ipc->use_count) ) {
      __close_ipc_resources(ipc);
    idx_allocator_destroy(&ipc->ports_array);
    idx_allocator_destroy(&ipc->channel_array);   
    memfree(ipc);
  }
}

void release_task_ipc_priv(task_ipc_priv_t *p)
{
  if( p->cached_data.cached_page1 ) {
    free_pages_addr(p->cached_data.cached_page1,IPC_PERTASK_PAGES);
  }
  if( p->cached_data.cached_page2 ) {
    free_pages_addr(p->cached_data.cached_page2,IPC_PERTASK_PAGES);
  }
  memfree(p);
}

int setup_task_ipc(task_t *task)
{
  task_ipc_t *ipc;
  task_ipc_priv_t *ipc_priv;
  void *p1,*p2;

  if( task->ipc ) {
    ipc=NULL;
  } else {
    ipc=memalloc(sizeof(*ipc));
    if( !ipc ) {
      return -ENOMEM;
    }
    memset(ipc,0,sizeof(task_ipc_t));
    atomic_set(&ipc->use_count,1);
    spinlock_initialize(&ipc->port_lock);
    spinlock_initialize(&ipc->channel_lock);
    mutex_initialize(&ipc->mutex);

    if( idx_allocator_init(&ipc->ports_array,CONFIG_IPC_DEFAULT_PORTS) ||
        idx_allocator_init(&ipc->channel_array,CONFIG_IPC_DEFAULT_CHANNELS) ) {
      goto free_ipc;
    }
  }

  ipc_priv=alloc_from_memcache(ipc_priv_data_cache);
  if( !ipc_priv ) {
    goto free_ipc;
  } else {
    memset(&ipc_priv->pstats,0,sizeof(ipc_pstats_t));
  }

  p1=alloc_pages_addr(IPC_PERTASK_PAGES, AF_ZERO);
  if( !p1 ) {
    goto free_ipc_priv;
  }

  p2=alloc_pages_addr(IPC_PERTASK_PAGES, AF_ZERO);
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
  free_pages_addr(p1, IPC_PERTASK_PAGES);
free_ipc_priv:
  release_task_ipc_priv(ipc_priv);
free_ipc:
  if( idx_allocator_initialized(&ipc->ports_array) ) {
    idx_allocator_destroy(&ipc->ports_array);
  }
  if( idx_allocator_initialized(&ipc->channel_array) ) {
    idx_allocator_destroy(&ipc->channel_array);
  }
  if(ipc) {
    release_task_ipc(ipc);
  }
  return -ENOMEM;
}

void *allocate_ipc_memory(long size)
{
  void *addr;

  if( size <= SLAB_OBJECT_MAX_SIZE ) {
    addr=memalloc(size);
    if( addr ) {
      memset(addr,0,size);
    }
  } else {
    addr=alloc_pages_addr(PAGE_ALIGN(size)>>PAGE_WIDTH,AF_ZERO);
  }
  return addr;
}

void free_ipc_memory(void *addr,int size)
{
  if( size <= SLAB_OBJECT_MAX_SIZE ) {
    memfree(addr);
  } else {
    int pages=size >> PAGE_WIDTH;

    if( size & PAGE_MASK ) {
      pages++;
    }
    free_pages_addr(addr,pages);
  }
}

long replicate_ipc(task_ipc_t *ipc,task_t *rcpt)
{
  long i,r=-ENOMEM;
  task_ipc_t *tipc;

  if( ipc ) {
    if( setup_task_ipc(rcpt) ) {
      return -ENOMEM;
    }

    tipc=rcpt->ipc;
    LOCK_IPC(ipc);
    if( ipc->ports ) { /* Duplicate all open ports. */
      tipc->ports=allocate_ipc_memory(ipc->allocated_ports*sizeof(ipc_gen_port_t *));
      if( !tipc->ports ) {
        goto out_unlock;
      }
      tipc->allocated_ports=ipc->allocated_ports;
      tipc->max_port_num=ipc->max_port_num;

      for(i=0;i<=ipc->max_port_num;i++) {
        if( ipc->ports[i] ) {
          tipc->ports[i]=ipc_clone_port(ipc->ports[i]);
          if( !tipc->ports[i] ) {
            UNLOCK_IPC(ipc);
            goto put_ports;
          }
          idx_reserve(&tipc->ports_array,i);
        }
      }
    }

    if( ipc->channels ) { /* Duplicate all open channels. */
      tipc->channels=allocate_ipc_memory(ipc->allocated_channels*sizeof(ipc_channel_t *));
      if( !tipc->channels ) {
        UNLOCK_IPC(ipc);
        goto put_ports;
      }
      tipc->allocated_channels=ipc->allocated_channels;
      tipc->max_channel_num=ipc->max_channel_num;

      for(i=0;i<=ipc->max_channel_num;i++) {
        if( ipc->channels[i] ) {
          tipc->channels[i]=ipc_clone_channel(ipc->channels[i],tipc);
          if( !tipc->channels[i] ) {
            UNLOCK_IPC(ipc);
            goto put_channels;
          }
          idx_reserve(&tipc->channel_array,i);
        }
      }
    }
  }

  r=0;
out_unlock:
  UNLOCK_IPC(ipc);
  return r;
put_channels:
  for(i=0;i<tipc->allocated_channels;i++) {
    if( tipc->channels[i] ) {
      memfree(tipc->channels[i]);
    }
  }
  free_ipc_memory(tipc->channels,tipc->allocated_channels*sizeof(ipc_channel_t *));
put_ports:
  for(i=0;i<tipc->allocated_ports;i++) {
    if( tipc->ports[i] ) {
      tipc->ports[i]->port_ops->destructor(tipc->ports[i]);
    }
  }
  free_ipc_memory(tipc->ports,tipc->allocated_ports*sizeof(ipc_gen_port_t *));
  release_task_ipc(tipc);
  return -ENOMEM;
}
