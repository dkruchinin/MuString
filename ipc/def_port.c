#include <eza/arch/types.h>
#include <ipc/port.h>
#include <eza/task.h>
#include <eza/errno.h>
#include <mm/pfalloc.h>
#include <mm/page.h>
#include <ds/linked_array.h>
#include <ipc/ipc.h>
#include <ipc/buffer.h>
#include <mlibc/stddef.h>
#include <eza/arch/preempt.h>
#include <eza/kconsole.h>
#include <mm/slab.h>
#include <ipc/gen_port.h>
#include <eza/arch/asm.h>

#define DEF_PORT_QUEUE_SIZE  ((PAGE_SIZE)/sizeof(long))

typedef struct __def_port_data_storage {
  list_head_t new_messages,all_messages;
  ipc_port_message_t **message_ptrs;
  linked_array_t msg_array;
} def_port_data_storage_t;

static int def_init_data_storage(struct __ipc_gen_port *port,
                                      task_t *owner,ulong_t queue_size)
{
  def_port_data_storage_t *ds;
  int r;

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

  r=linked_array_initialize(&ds->msg_array,queue_size);
  if( r ) {
    goto free_messages;
  }

  list_init_head(&ds->new_messages);
  list_init_head(&ds->all_messages);
  port->data_storage=ds;
  port->capacity=queue_size;
  return 0;
free_messages:
  free_pages_addr(ds->message_ptrs);
free_ds:
  memfree(ds);
  return -ENOMEM;
}

static int def_insert_message(struct __ipc_gen_port *port,
                                   ipc_port_message_t *msg)
{
  def_port_data_storage_t *ds=(def_port_data_storage_t *)port->data_storage;
  ulong_t id=linked_array_alloc_item(&ds->msg_array);

  if( id != INVALID_ITEM_IDX ) {
    msg->id=id;
    list_add2tail(&ds->new_messages,&msg->l);
    list_add2tail(&ds->all_messages,&msg->messages_list);
    ds->message_ptrs[id]=msg;
    port->avail_messages++;
    port->total_messages++;
    return 0;
  }
  return -ENOMEM;  
}

/* NOTE: port must be locked before calling this function ! */
static ipc_port_message_t *def_extract_message(ipc_gen_port_t *p,ulong_t flags)
{
  ipc_port_message_t *msg;
  def_port_data_storage_t *ds=(def_port_data_storage_t*)p->data_storage;

  if( !list_is_empty(&ds->new_messages) ) {
    msg = container_of(list_node_first(&ds->new_messages),ipc_port_message_t,l);
    list_del(&msg->l);
    p->avail_messages--;
    return msg;
  }
  return NULL;
}

static void def_free_data_storage(struct __ipc_gen_port *port)
{
}

static int def_remove_message(struct __ipc_gen_port *port,
                                   ipc_port_message_t *msg)
{
  if( msg->id < port->capacity ) {
    def_port_data_storage_t *ds=(def_port_data_storage_t *)port->data_storage;

    if( list_node_is_bound(&msg->l) ) {
      port->avail_messages--;
      list_del(&msg->l);
    }
    list_del(&msg->messages_list);
    linked_array_free_item(&ds->msg_array,msg->id);
    ds->message_ptrs[msg->id]=NULL;
    port->total_messages--;
    return 0;
  }

  return -EINVAL;
}

static ipc_port_message_t *def_remove_head_message(struct __ipc_gen_port *port)
{
  def_port_data_storage_t *ds=(def_port_data_storage_t *)port->data_storage;
  
  if( !list_is_empty(&ds->all_messages) ) {
    ipc_port_message_t *msg;

    msg = container_of(list_node_first(&ds->all_messages),ipc_port_message_t,
                       messages_list);
    list_del(&msg->messages_list);

    if( list_node_is_bound(&msg->l) ) {
      port->avail_messages--;
      list_del(&msg->l);
    }

    ds->message_ptrs[msg->id]=NULL;
    port->total_messages--;
    linked_array_free_item(&ds->msg_array,msg->id);
    return msg;
  }

  return NULL;
}

static void def_dequeue_message(struct __ipc_gen_port *port,
                                ipc_port_message_t *msg)
{
  def_port_data_storage_t *ds=(def_port_data_storage_t *)port->data_storage;

  ASSERT(list_node_is_bound(&msg->messages_list));

  if( list_node_is_bound(&msg->l) ) {
    port->avail_messages--;
    list_del(&msg->l);
  }

  list_del(&msg->messages_list);
  ds->message_ptrs[msg->id]=__MSG_WAS_DEQUEUED;
}

static ipc_port_message_t *def_lookup_message(struct __ipc_gen_port *port,
                                              ulong_t msg_id)
{
  def_port_data_storage_t *ds=(def_port_data_storage_t *)port->data_storage;

  if( msg_id < port->capacity ) {
    ipc_port_message_t *msg=ds->message_ptrs[msg_id];

    if( msg == __MSG_WAS_DEQUEUED ) { /* Deffered cleanup. */
      ds->message_ptrs[msg_id]=NULL;
      port->total_messages--;
      linked_array_free_item(&ds->msg_array,msg_id);
    }
    return msg;
  }
  return NULL;
}

ipc_port_msg_ops_t def_port_msg_ops = {
  .init_data_storage=def_init_data_storage,
  .insert_message=def_insert_message,
  .free_data_storage=def_free_data_storage,
  .extract_message=def_extract_message,
  .remove_message=def_remove_message,
  .remove_head_message=def_remove_head_message,
  .dequeue_message=def_dequeue_message,
  .lookup_message=def_lookup_message,
};
