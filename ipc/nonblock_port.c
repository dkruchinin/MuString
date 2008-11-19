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

static status_t nb_init_data_storage(struct __ipc_gen_port *port,
                                     task_t *owner)
{
  return 0;
}

static status_t nb_insert_message(struct __ipc_gen_port *port,
                                   ipc_port_message_t *msg,ulong_t flags)
{
  return 0;
}

static void nb_free_data_storage(struct __ipc_gen_port *port)
{
}

ipc_port_msg_ops_t nonblock_port_msg_ops = {
  .init_data_storage = nb_init_data_storage,
  .insert_message = nb_insert_message,
  .free_data_storage = nb_free_data_storage,
};
