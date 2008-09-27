#include <eza/arch/types.h>
#include <ipc/ipc.h>
#include <mm/pfalloc.h>
#include <mm/page.h>
#include <ds/linked_array.h>

void initialize_ipc(void)
{
}

task_ipc_t *allocate_task_ipc(void)
{
  page_frame_t *p = alloc_page(AF_PGEN | AF_ZERO);
  if( p!= NULL ) {
    task_ipc_t *ipc = (task_ipc_t*)pframe_to_virt(p);

    atomic_set(&ipc->use_count,1);
    ipc->num_ports = 0;
    ipc->ports = NULL;
    semaphore_initialize(&ipc->sem,1);
    return ipc;
  }
  return NULL;
  /* TODO: [mt] allocate task_ipc_t via slabs ! */
}
