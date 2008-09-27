#include <eza/arch/types.h>
#include <ipc/port.h>
#include <eza/task.h>
#include <eza/errno.h>
#include <eza/semaphore.h>
#include <mm/pfalloc.h>
#include <mm/page.h>
#include <ds/linked_array.h>
#include <ipc/ipc.h>

static status_t __allocate_port(ipc_port_t **out_port,ulong_t flags)
{
  ipc_port_t *p = alloc_pages_addr(1,AF_PGEN|AF_ZERO);
  if( p == NULL ) {
    return -ENOMEM;
  }

  p->message_ptrs = alloc_pages_addr(1,AF_PGEN|AF_ZERO);
  if( p->message_ptrs == NULL ) {
    return -ENOMEM;
  }

  if( !(flags & IPC_BLOCKED_ACCESSS) ) {
    kprintf( KO_WARNING "Non-blocking ports aren't implemented yet !\n" );
    return -EINVAL;
  }

  atomic_set(&p->use_count,1);
  spinlock_initialize(&p->lock, "");
  /* TODO: [mt] allocate memory via slabs ! */
  linked_array_initialize(&p->msg_list,IPC_DEFAULT_PORT_MESSAGES);
  p->queue_size = IPC_DEFAULT_PORT_MESSAGES;
  p->flags = flags;
  p->avail_messages = 0;

  *out_port = p;
  return 0;
}

status_t ipc_create_port(task_t *owner,ulong_t flags,ulong_t size)
{
  status_t r;
  task_ipc_t *ipc = owner->ipc;
  ulong_t id;
  ipc_port_t *port;

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
    r = -ENOMEM;
    ipc->ports = alloc_pages_addr(1,AF_PGEN|AF_ZERO);
    if( !ipc->ports ) {
      goto out_unlock;
    }

    if( linked_array_initialize(&ipc->ports_array,IPC_DEFAULT_PORTS) != 0 ) {
      /* TODO: [mt] allocate/free memory via slabs. */
      goto out_unlock;
    }
  }

  id = linked_array_alloc_item(&ipc->ports_array);
  if(id == INVALID_ITEM_IDX) {
    r = -EMFILE;
    goto out_unlock;
  }

  /* Ok, it seems that we can create a new port. */
  r = __allocate_port(&port,flags);
  if( r != 0 ) {
    linked_array_free_item(&ipc->ports_array,id);
    goto out_unlock;
  }

  /* Install new port. */
  ipc->ports[id] = port;
  ipc->num_ports++;
  kprintf( "++ PORT ADDRESS: %p\n", port );
  r = id;
out_unlock:
  UNLOCK_IPC(ipc);
  return r;
}
