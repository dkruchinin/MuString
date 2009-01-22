#include <eza/arch/types.h>
#include <ipc/port.h>
#include <eza/task.h>
#include <eza/errno.h>
#include <mm/pfalloc.h>
#include <mm/page.h>
#include <ds/linked_array.h>
#include <ipc/ipc.h>
#include <eza/container.h>
#include <ipc/buffer.h>
#include <mm/slab.h>
#include <ipc/gen_port.h>
#include <ds/list.h>

typedef struct __prio_port_data_storage {
  list_head_t prio_head,all_messages,id_waiters;
  ipc_port_message_t **message_ptrs;
  linked_array_t msg_array;
  ulong_t num_waiters;
} prio_port_data_storage_t;

static status_t prio_init_data_storage(struct __ipc_gen_port *port,
                                      task_t *owner)
{
  prio_port_data_storage_t *ds;
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

  list_init_head(&ds->prio_head);
  list_init_head(&ds->all_messages);
  list_init_head(&ds->id_waiters);
  ds->num_waiters=0;
  port->data_storage=ds;
  port->capacity=owner->limits->limits[LIMIT_IPC_MAX_PORT_MESSAGES];
  return 0;
free_messages:
  free_pages_addr(ds->message_ptrs);
free_ds:
  memfree(ds);
  return -ENOMEM;
}

static void __add_one_message(list_head_t *list,
                              ipc_port_message_t *msg)
{
  if( list_is_empty(list) ) {
    list_add2head(list,&msg->l);
  } else {
    list_node_t *next=list->head.next,*prev=NULL;
    bool inserted=false;
    ulong_t p2=msg->sender->static_priority;

    do {
      ipc_port_message_t *m=container_of(next,ipc_port_message_t,l);
      ulong_t p1=m->sender->static_priority;

      if( p1 > p2 ) {
        break;
      } else if( p1 == p2 ) {
        list_add2tail(&m->h,&msg->l);
        inserted=true;
      }
      prev=next;
      next=next->next;
    } while(next != list_head(list) );

    if( !inserted ) {
      if( prev != NULL ) {
        msg->l.next=prev->next;
        prev->next->prev=&msg->l;
        prev->next=&msg->l;
        msg->l.prev=prev;
      } else {
        list_add2head(list,&msg->l);
      }
    }
  }
}

static void __remove_message(ipc_port_message_t *msg)
{
  list_node_t *prev=msg->l.prev;

  list_del(&msg->l);
  if( !list_is_empty(&msg->h) ) {
    ipc_port_message_t *m=container_of(list_node_first(&msg->h),
                                       ipc_port_message_t,l);
    list_del(&m->l);
    if( !list_is_empty(&msg->h) ) {
      list_move2head(&m->h,&msg->h);
    }
    m->l.prev=prev;
    m->l.next=prev->next;
    prev->next->prev=&m->l;
    prev->next=&m->l;
  }
}

static status_t prio_insert_message(struct __ipc_gen_port *port,
                                   ipc_port_message_t *msg)
{
  prio_port_data_storage_t *ds=(prio_port_data_storage_t *)port->data_storage;
  ulong_t id=linked_array_alloc_item(&ds->msg_array);

  if( id != INVALID_ITEM_IDX ) {
    msg->id=id;
    ds->message_ptrs[id]=msg;
    port->avail_messages++;
    port->total_messages++;

    list_init_head(&msg->h);
    list_init_node(&msg->l);
    list_add2tail(&ds->all_messages,&msg->messages_list);

    /* Now insert message according to its priority. */
    __add_one_message(&ds->prio_head,msg);
   }
  return -ENOMEM;
}

static ipc_port_message_t *prio_extract_message(ipc_gen_port_t *p,ulong_t flags)
{
  prio_port_data_storage_t *ds=(prio_port_data_storage_t*)p->data_storage;

  if( !list_is_empty(&ds->prio_head) ) {
    ipc_port_message_t *msg=container_of(list_node_first(&ds->prio_head),
                                         ipc_port_message_t,l);
    __remove_message(msg);
    p->avail_messages--;
    return msg;
  }
  return NULL;
}

static void prio_free_data_storage(struct __ipc_gen_port *port)
{
}

static status_t prio_remove_message(struct __ipc_gen_port *port,
                                    ipc_port_message_t *msg)
{
  prio_port_data_storage_t *ds=(prio_port_data_storage_t*)port->data_storage;

  if( msg->id < port->capacity ) {
    linked_array_free_item(&ds->msg_array,msg->id);
    if( list_node_is_bound(&msg->l) ) {
      port->avail_messages--;
    }
  } else if( msg->id == WAITQUEUE_MSG_ID ) {
    ds->num_waiters--;
  } else {
    return -EINVAL;
  }

  port->total_messages--;
  list_del(&msg->messages_list);

  if( list_node_is_bound(&msg->l) ) {
    __remove_message(msg);
  }

  return 0;
}

static ipc_port_message_t *prio_remove_head_message(struct __ipc_gen_port *port)
{
  prio_port_data_storage_t *ds=(prio_port_data_storage_t*)port->data_storage;

  if( !list_is_empty(&ds->all_messages) ) {
    ipc_port_message_t *msg=container_of(list_node_first(&ds->all_messages),
                                         ipc_port_message_t,messages_list);
    prio_remove_message(port,msg);
    return msg;
  }
  return NULL;
}

static void prio_dequeue_message(struct __ipc_gen_port *port,
                                 ipc_port_message_t *msg)
{
}

static ipc_port_message_t *prio_lookup_message(struct __ipc_gen_port *port,
                                               ulong_t msg_id)
{
  if( msg_id < port->capacity ) {
    return ((prio_port_data_storage_t*)port->data_storage)->message_ptrs[msg_id];
  }
  return NULL;
}

ipc_port_msg_ops_t prio_port_msg_ops = {
  .init_data_storage=prio_init_data_storage,
  .insert_message=prio_insert_message,
  .free_data_storage=prio_free_data_storage,
  .extract_message=prio_extract_message,
  .remove_message=prio_remove_message,
  .remove_head_message=prio_remove_head_message,
  .dequeue_message=prio_dequeue_message,
  .lookup_message=prio_lookup_message,
};
