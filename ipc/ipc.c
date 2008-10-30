#include <eza/arch/types.h>
#include <ipc/ipc.h>
#include <mm/pfalloc.h>
#include <mm/page.h>
#include <ds/linked_array.h>
#include <mlibc/string.h>
#include <mm/slab.h>

void initialize_ipc(void)
{
}

task_ipc_t *allocate_task_ipc(void)
{
  page_frame_t *p = alloc_page(AF_PGEN | AF_ZERO);
  if( p!= NULL ) {
    task_ipc_t *ipc = (task_ipc_t*)pframe_to_virt(p);
    
    memset(ipc,0,sizeof(task_ipc_t));

    atomic_set(&ipc->use_count,1);

    spinlock_initialize(&ipc->port_lock, "");
    semaphore_initialize(&ipc->sem,1);
    spinlock_initialize(&ipc->buffer_lock, "");

    ipc->cached_data.cached_page1 = alloc_pages_addr(1, AF_PGEN | AF_ZERO);
    ipc->cached_data.cached_page2 = alloc_pages_addr(1, AF_PGEN | AF_ZERO);

    /* TODO: [mt] Check for NULL during memory allocation ! [R] */
    ipc->pstats=(ipc_pstats_t*)memalloc(sizeof(ipc_pstats_t));
    memset(ipc->pstats,0,sizeof(ipc_pstats_t));
    atomic_set(&ipc->pstats->active_queues,0);

    return ipc;
  }
  return NULL;
  /* TODO: [mt] allocate task_ipc_t via slabs ! */
}

void get_task_ipc(task_t *t)
{
  if( t && t->ipc ) {
    atomic_inc(&t->ipc->use_count);
  }
}

void release_task_ipc(task_t *t)
{
  if( t && t->ipc ) {
    atomic_dec(&t->ipc->use_count);
  }
}
