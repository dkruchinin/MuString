#include <ipc/port.h>
#include <ipc/ipc.h>
#include <eza/arch/arch_ipc.h>
#include <eza/arch/context.h>
#include <mlibc/kprintf.h>
#include <eza/arch/types.h>
#include <eza/arch/task.h>
#include <eza/errno.h>

#define AMD64_MAX_PULSE_SIZE  64

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

status_t arch_copy_port_message_to_receiver(task_t *receiver,
                                            ipc_port_message_t *message)
{
  regs_t *sender_regs = arch_get_syscall_stack_frame(receiver);
  regs_t *receiver_regs = arch_get_syscall_stack_frame(receiver);
  status_t r;

  /* TODO: [mt] Implement data transfers for all port types, not only 'pulses'. */
  switch(message->type) {
    case PORT_MESSAGE_PULSE:
      r = 0;
      break;
    default:
      kprintf( KO_WARNING "arch_copy_port_message_to_receiver(): Unknown message type:\n",
               message->type);
      r=-EINVAL;
      break;
  }

  return r;
}
