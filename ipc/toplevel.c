#include <eza/arch/types.h>
#include <ipc/ipc.h>
#include <ipc/port.h>
#include <ipc/buffer.h>
#include <eza/task.h>
#include <eza/smp.h>
#include <kernel/syscalls.h>
#include <eza/errno.h>
#include <eza/process.h>

/* TODO: [mt] Implement security checks for port-related syscalls ! */

status_t sys_create_port( ulong_t flags, ulong_t queue_size )
{
  return ipc_create_port(current_task(),flags,queue_size);
}

status_t sys_port_send(pid_t pid,ulong_t port,uintptr_t snd_buf,
                       ulong_t snd_size,uintptr_t rcv_buf,ulong_t rcv_size)
{
  task_t *task = pid_to_task(pid);
  status_t r;

  if( task == NULL ) {
    return -ESRCH;
  }

  r=ipc_port_send(task,port,snd_buf,snd_size,rcv_buf,rcv_size);
  release_task_struct(task);
  return r;
}

status_t sys_port_receive( ulong_t port, ulong_t flags, ulong_t recv_buf,
                           ulong_t recv_len)
{
  ipc_port_receive_stats_t rcv_stats;
  status_t r;

  r=ipc_port_receive(current_task(),port,flags,recv_buf,recv_len,&rcv_stats);
  if( !r ) {
    r=ENCODE_RECV_RETURN_VALUE(rcv_stats.msg_id,rcv_stats.bytes_received);
  }

  return r;
}

status_t sys_port_reply(ulong_t port, ulong_t msg_id,ulong_t reply_buf,
                        ulong_t reply_len)
{
  return ipc_port_reply(current_task(),port,msg_id,reply_buf,reply_len);
}
