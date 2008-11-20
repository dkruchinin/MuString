#include <eza/arch/types.h>
#include <ipc/ipc.h>
#include <ipc/port.h>
#include <ipc/buffer.h>
#include <eza/task.h>
#include <eza/smp.h>
#include <kernel/syscalls.h>
#include <eza/errno.h>
#include <eza/process.h>
#include <eza/security.h>
#include <ipc/gen_port.h>
#include <ipc/channel.h>
#include <kernel/vm.h>

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
                           ulong_t recv_len,port_msg_info_t *msg_info)
{
  return ipc_port_receive(current_task(),port,flags,recv_buf,recv_len,msg_info);
}

status_t sys_port_reply(ulong_t port, ulong_t msg_id,ulong_t reply_buf,
                        ulong_t reply_len)
{
  return ipc_port_reply(current_task(),port,msg_id,reply_buf,reply_len);
}

status_t sys_open_channel(pid_t pid,ulong_t port)
{
  task_t *task = pid_to_task(pid);
  status_t r;

  if( task == NULL ) {
    return -ESRCH;
  }

  r=ipc_open_channel(current_task(),task,port);
  release_task_struct(task);
  return r;
}

status_t __sys_create_port( ulong_t flags, ulong_t queue_size )
{
  task_t *caller=current_task();

  if( !trusted_task(caller) ) {
    flags |= IPC_BLOCKED_ACCESS;
  }

  return __ipc_create_port(caller,flags);
}

status_t __sys_port_receive(ulong_t port, ulong_t flags, ulong_t recv_buf,
                          ulong_t recv_len,port_msg_info_t *msg_info)
{
  ipc_gen_port_t *p;
  status_t r;

  r=__ipc_get_port(current_task(),port,&p);
  if(r) {
    return r;
  }

  if( !valid_user_address((ulong_t)msg_info) ||
      !valid_user_address(recv_buf) ) {
    r=-EFAULT;
    goto put_port;
  }

  r=__ipc_port_receive(p,flags,recv_buf,recv_len,msg_info);
put_port:
  __ipc_put_port(p);
  return r;
}

status_t __sys_port_send(pid_t pid,ulong_t port,uintptr_t snd_buf,
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
