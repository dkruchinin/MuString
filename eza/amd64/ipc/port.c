#include <ipc/port.h>
#include <ipc/ipc.h>
#include <eza/arch/arch_ipc.h>
#include <eza/arch/context.h>
#include <mlibc/kprintf.h>
#include <eza/arch/types.h>
#include <eza/arch/task.h>
#include <eza/errno.h>

status_t arch_setup_port_message_buffers(task_t *caller,
                                         ipc_port_message_t *message)
{
    if( message->flags & IPC_BLOCKED_ACCESS ) {
        message->send_buffer = arch_get_syscall_stack_frame(caller);
        message->receive_buffer = message->send_buffer;
        return 0;
    } else {
        kprintf( KO_WARNING "Non-blocking ports aren't implemented yet !\n" );
        return -EINVAL;
    }
}
