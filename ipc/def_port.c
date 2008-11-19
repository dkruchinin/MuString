#include <eza/arch/types.h>
#include <ipc/port.h>
#include <eza/task.h>
#include <eza/errno.h>
#include <eza/semaphore.h>
#include <mm/pfalloc.h>
#include <mm/page.h>
#include <ds/linked_array.h>
#include <ipc/ipc.h>
#include <eza/container.h>
#include <ipc/buffer.h>
#include <mlibc/stddef.h>
#include <kernel/vm.h>
#include <eza/arch/preempt.h>
#include <eza/kconsole.h>
#include <mm/slab.h>
#include <ipc/gen_port.h>

typedef struct __def_port_data_storage {
  list_head_t messages;
  ipc_port_message_t **message_ptrs;
  linked_array_t msg_array;
} def_port_data_storage_t;

static status_t def_init_data_storage(struct __ipc_gen_port *port,
                                      task_t *owner)
{
  def_port_data_storage_t *ds;
  status_t r;

  if( port->data_storage ) {
    return -EBUSY;
  }

  ds=memalloc(sizeof(*ds));
  if( !ds ) {
    return -ENOMEM;
  }

  ds->message_ptrs = alloc_pages_addr(1,AF_PGEN|AF_ZERO);
  if( ds->message_ptrs == NULL ) {
    goto free_ds;
  }

  r=linked_array_initialize(&ds->msg_array,
                            owner->limits->limits[LIMIT_IPC_MAX_PORT_MESSAGES]);
  if( r ) {
    goto free_messages;
  }

  return 0;
free_messages:
  free_pages_addr(ds->message_ptrs);
free_ds:
  memfree(ds);
  return -ENOMEM;
}

static status_t def_insert_message(struct __ipc_gen_port *port,
                                   ipc_port_message_t *msg,ulong_t flags)
{
  return 0;
}

/* NOTE: port must be locked before calling this function ! */
static ipc_port_message_t *def_extract_message(ipc_gen_port_t *p,ulong_t flags)
{
  ipc_port_message_t *msg;
  def_port_data_storage_t *ds=(def_port_data_storage_t*)p->data_storage;
  list_node_t *lm = list_node_first(&ds->messages);

  msg = container_of(list_node_first(&ds->messages),ipc_port_message_t,l);
  list_del(lm);
  p->avail_messages--;
  return msg;
}

static void def_free_data_storage(struct __ipc_gen_port *port)
{
}

ipc_port_msg_ops_t def_port_msg_ops = {
  .init_data_storage = def_init_data_storage,
  .insert_message = def_insert_message,
  .free_data_storage = def_free_data_storage,
  .extract_message = def_extract_message,
};
