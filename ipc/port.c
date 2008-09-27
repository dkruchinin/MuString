#include <eza/arch/types.h>
#include <ipc/port.h>
#include <eza/task.h>
#include <eza/errno.h>
#include <eza/semaphore.h>
#include <mm/pfalloc.h>
#include <mm/page.h>

void initialize_ipc(void)
{
}

status_t ipc_create_port(task_t *owner,ulong_t mode,ulong_t size)
{
  status_t r;
  task_ipc_t *ipc = owner->ipc;

  LOCK_IPC(ipc);

  if(size > owner->limits->limits[LIMIT_IPC_MAX_PORT_MESSAGES] ) {
    r = -E2BIG;
    goto out_unlock;
  }

  if(ipc->num_ports >= owner->limits->limits[LIMIT_IPC_MAX_PORTS]) {
    r = -EMFILE;
    goto out_unlock;
  }

  /* First port created ? */
  if( !ipc->ports ) {
    ipc->ports = alloc_pages_addr(1,AF_PGEN|AF_ZERO);
    if( !ipc->ports ) {
      r = -ENOMEM;
      goto out_unlock;
    }
  }

  r = 0;
out_unlock:
  UNLOCK_IPC(ipc);
  return r;
}
