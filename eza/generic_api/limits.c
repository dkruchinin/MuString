#include <eza/arch/types.h>
#include <eza/limits.h>
#include <mm/pfalloc.h>
#include <mm/page.h>
#include <ipc/ipc.h>
#include <eza/spinlock.h>

task_limits_t *allocate_task_limits(void)
{
  page_frame_t *p = alloc_page(AF_ZERO);
  if( p!= NULL ) {
    task_limits_t *tl = (task_limits_t*)pframe_to_virt(p);

    atomic_set(&tl->use_count,1);
    spinlock_initialize(&tl->lock);
    return tl;
  }
  return NULL;
  /* TODO: [mt] Allocate task_limits_t via slabs ! */
}

void set_default_task_limits(task_limits_t *l)
{
  l->limits[LIMIT_IPC_MAX_PORTS]=IPC_DEFAULT_PORTS;
  l->limits[LIMIT_IPC_MAX_PORT_MESSAGES]=IPC_DEFAULT_PORT_MESSAGES;
  l->limits[LIMIT_IPC_MAX_USER_BUFFERS]=IPC_DEFAULT_BUFFERS;
  l->limits[LIMIT_IPC_MAX_CHANNELS]=IPC_DEFAULT_CHANNELS;
}
