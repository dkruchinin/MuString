#include <ipc/port.h>
#include <eza/arch/arch_ipc.h>

arch_ipc_port_ctx_t send_ctx,recv_ctx;

arch_ipc_port_ctx_t *arch_ipc_get_sender_port_ctx(task_t *caller)
{
    return &send_ctx;
}

arch_ipc_port_ctx_t *arch_ipc_get_receiver_port_ctx(task_t *caller)
{
    return &recv_ctx;
}

