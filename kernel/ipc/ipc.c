#include <arch/types.h>
#include <ipc/ipc.h>
#include <mm/page_alloc.h>
#include <mm/page.h>
#include <ds/idx_allocator.h>
#include <mstring/string.h>
#include <mm/slab.h>
#include <mstring/errno.h>
#include <mstring/kconsole.h>
#include <mstring/string.h>
#include <config.h>

static memcache_t *ipc_priv_data_cache;

void initialize_ipc(void)
{

  ipc_priv_data_cache= create_memcache("IPC private data",
                                        sizeof(task_ipc_priv_t),1,
                                       MMPOOL_KERN | SMCF_IMMORTAL |
                                       SMCF_LAZY | SMCF_UNIQUE);

  if( !ipc_priv_data_cache ) {
    panic( "initialize_ipc(): Can't create the IPC private data memcache !" );
  }
}


static void __close_ipc_resources(task_ipc_t *ipc)
{
  uint32_t i;

  /* Close all open ports. */
  if( !hat_is_empty(&ipc->ports) ) {
    for( i = 0; i <= ipc->max_port_num; i++) {
      if( hat_lookup(&ipc->ports, i) ) {
        ipc_close_port(ipc, i);
      }
    }
  }

  /* Close all channels. */
  if( !hat_is_empty(&ipc->channels)) {
    for(i=0; i<=ipc->max_channel_num; i++) {
      if( hat_lookup(&ipc->channels, i) ) {
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
    hat_destroy(&ipc->ports);
    hat_destroy(&ipc->channels);
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
    ipc = memalloc(sizeof(*ipc));
    if( !ipc ) {
        return ERR(-ENOMEM);
  }

    memset(ipc,0,sizeof(task_ipc_t));
    atomic_set(&ipc->use_count,1);

    if ( hat_initialize(&ipc->channels, CONFIG_TASK_CHANNELS_LIMIT_MAX))
      goto free_ipc;
    ipc->allocated_channels = CONFIG_TASK_CHANNELS_LIMIT_MAX;

    if ( hat_initialize(&ipc->ports, CONFIG_TASK_PORTS_LIMIT_MAX) )
      goto free_ipc;
    ipc->allocated_ports = CONFIG_TASK_PORTS_LIMIT_MAX;

    spinlock_initialize(&ipc->port_lock, "Port");
    spinlock_initialize(&ipc->channel_lock, "Channel");
    mutex_initialize(&ipc->mutex);

    if( idx_allocator_init(&ipc->ports_array, CONFIG_TASK_PORTS_LIMIT_MAX) ||
        idx_allocator_init(&ipc->channel_array, CONFIG_TASK_CHANNELS_LIMIT_MAX) ) {
      goto free_ipc;
    }
  }

  ipc_priv = alloc_from_memcache(ipc_priv_data_cache, 0);
  if( !ipc_priv ) {
    goto free_ipc;
  } else {
    memset(&ipc_priv->pstats,0,sizeof(ipc_pstats_t));
  }

  p1=alloc_pages_addr(IPC_PERTASK_PAGES, MMPOOL_KERN | AF_ZERO | AF_CONTIG);
  if( !p1 ) {
    goto free_ipc_priv;
  }

  p2=alloc_pages_addr(IPC_PERTASK_PAGES, MMPOOL_KERN | AF_ZERO | AF_CONTIG);
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
  return ERR(-ENOMEM);
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
    addr=alloc_pages_addr(PAGE_ALIGN(size)>>PAGE_WIDTH,MMPOOL_KERN | AF_ZERO | AF_CONTIG);
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
  ipc_gen_port_t *tport;
  ipc_channel_t *tchan;
  if( ipc ) {
    if( setup_task_ipc(rcpt) ) {
        return ERR(-ENOMEM);
    }

    tipc=rcpt->ipc;
    LOCK_IPC(ipc);
    if( !hat_is_empty(&ipc->ports) ) { /* Duplicate all open ports. */

     /* if( hat_initialize(&tipc->ports, ipc->ports.size) != 0 ) {
        goto out_unlock;
      } */
      //tipc->allocated_ports=ipc->allocated_ports;
      tipc->max_port_num = ipc->max_port_num;
      tipc->num_ports = ipc->num_ports;

      for(i = 0; i <= ipc->max_port_num; i++) {
        tport = hat_lookup(&ipc->ports, i);
        if(tport) {
          hat_insert(&tipc->ports, i, ipc_clone_port(tport));
          if( !hat_lookup(&tipc->ports, i) ) {
            UNLOCK_IPC(ipc);
            goto put_ports;
          }
          idx_reserve(&tipc->ports_array,i);
        }
      }
    }

    if( !hat_is_empty(&ipc->channels) ) { /* Duplicate all open channels. */

     /* if( hat_initialize(&tipc->channels, ipc->channels.size) != 0 ) {
        UNLOCK_IPC(ipc);
        goto put_ports;
      }*/
      //tipc->allocated_channels=ipc->allocated_channels;
      tipc->max_channel_num=ipc->max_channel_num;
      tipc->num_channels = ipc->num_channels;

      for(i=0; i<=ipc->max_channel_num; i++) {
        tchan = hat_lookup(&ipc->channels, i);
        if( tchan ) {
          hat_insert(&tipc->channels, i, ipc_clone_channel(tchan, tipc));
          if( !hat_lookup(&tipc->channels, i) ) {
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
  return ERR(r);
put_channels:
  for( i = 0; i < tipc->allocated_channels; i++) {
    tchan = hat_lookup(&tipc->channels, i);
    if( tchan ) {
      memfree(&tchan);
    }
  }

put_ports:
  for( i = 0; i < tipc->allocated_ports; i++) {
    tport = hat_lookup(&tipc->ports, i);
    if( tport) {
      tport->port_ops->destructor(tport);
    }
  }
  release_task_ipc(tipc);
  return ERR(-ENOMEM);
}
