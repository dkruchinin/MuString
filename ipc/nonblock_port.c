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
#include <ipc/gen_port.h>

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

typedef struct __nb_port_data_storage {
  list_head_t messages;
} nb_port_data_storage_t;

static status_t nb_init_data_storage(struct __ipc_gen_port *port,
                                      task_t *owner)
{
  nb_port_data_storage_t *nb;

  if( port->data_storage ) {
    return -EBUSY;
  }

  nb=memalloc(sizeof(*nb));
  if( !nb ) {
    return -ENOMEM;
  }

  list_init_head(&nb->messages);
  port->data_storage=nb;
  return 0;
}

static status_t nb_insert_message(struct __ipc_gen_port *port,
                                   ipc_port_message_t *msg,ulong_t flags)
{
  nb_port_data_storage_t *nb=(nb_port_data_storage_t *)port->data_storage;

  list_add2tail(&nb->messages,&msg->l);
  msg->id=INVALID_ITEM_IDX;
  port->avail_messages++;
  return 0;
}

static ipc_port_message_t *nb_extract_message(ipc_gen_port_t *p,ulong_t flags)
{
  ipc_port_message_t *msg;
  nb_port_data_storage_t *ds=(nb_port_data_storage_t*)p->data_storage;
  list_node_t *lm = list_node_first(&ds->messages);

  msg = container_of(list_node_first(&ds->messages),ipc_port_message_t,l);
  list_del(lm);
  p->avail_messages--;
  return msg;
}

static void nb_free_data_storage(struct __ipc_gen_port *port)
{
}

static void nb_requeue_message(struct __ipc_gen_port *port,ipc_port_message_t *msg)
{
  nb_port_data_storage_t *ds=(nb_port_data_storage_t*)port->data_storage;

  list_add2head(&ds->messages,&msg->l);
  port->avail_messages++;
}

static void nb_post_receive_logic(struct __ipc_gen_port *port,ipc_port_message_t *msg)
{
  memfree(msg);
}

ipc_port_msg_ops_t nonblock_port_msg_ops = {
  .init_data_storage=nb_init_data_storage,
  .insert_message=nb_insert_message,
  .free_data_storage=nb_free_data_storage,
  .extract_message=nb_extract_message,
  .requeue_message=nb_requeue_message,
  .post_receive_logic=nb_post_receive_logic,
};

