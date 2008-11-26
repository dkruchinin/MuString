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

status_t sys_close_channel(ulong_t channel)
{
  return ipc_close_channel(current_task(),channel);
}

status_t sys_create_port( ulong_t flags, ulong_t queue_size )
{
  task_t *caller=current_task();

  if( !trusted_task(caller) ) {
    flags |= IPC_BLOCKED_ACCESS;
  }

  return __ipc_create_port(caller,flags);
}

status_t sys_close_port(ulong_t port)
{
  return ipc_close_port(current_task(),port);
}

status_t sys_port_reply(ulong_t port,ulong_t msg_id,ulong_t reply_buf,
                        ulong_t reply_len) {
  ipc_gen_port_t *p;
  status_t r;

  if( !reply_buf || reply_len > MAX_PORT_MSG_LENGTH ) {
    return -EINVAL;
  }

  if( !valid_user_address_range(reply_buf,reply_len) ) {
    return -EFAULT;
  }

  p=__ipc_get_port(current_task(),port);
  if( !p ) {
    return -EINVAL;
  }

  r=__ipc_port_reply(p,msg_id,reply_buf,reply_len);
  __ipc_put_port(p);
  return r;
}

status_t sys_port_receive(ulong_t port, ulong_t flags, ulong_t recv_buf,
                          ulong_t recv_len,port_msg_info_t *msg_info)
{
  ipc_gen_port_t *p;
  status_t r;

  if( !valid_user_address_range((ulong_t)msg_info,sizeof(*msg_info)) ||
      !valid_user_address_range(recv_buf,recv_len) ) {
    return -EFAULT;
  }

  p=__ipc_get_port(current_task(),port);
  if( !p ) {
    return -EINVAL;
  }

  r=__ipc_port_receive(p,flags,recv_buf,recv_len,msg_info);
  __ipc_put_port(p);
  return r;
}

status_t sys_port_send(ulong_t channel,ulong_t flags,
                       uintptr_t snd_buf,ulong_t snd_size,
                       uintptr_t rcv_buf,ulong_t rcv_size)
{
  task_t *caller=current_task();
  ipc_channel_t *c=ipc_get_channel(caller,channel);
  ipc_gen_port_t *port;
  ipc_port_message_t *msg;
  status_t r=-EINVAL;

  if( c == NULL ) {
    return -EINVAL;
  }

  if( !valid_user_address_range((ulong_t)snd_buf,snd_size) ||
      !valid_user_address_range(rcv_buf,rcv_size) ) {
    return -EFAULT;
  }
  
  if( !trusted_task(caller) ) {
    if( (flags & UNTRUSTED_MANDATORY_FLAGS) != UNTRUSTED_MANDATORY_FLAGS ) {
      return -EPERM;
    }
  }

  LOCK_CHANNEL(c);
  port=c->server_port;
  if( port ) {
    REF_IPC_ITEM(port);
  }
  UNLOCK_CHANNEL(c);

  if( !port ) {
    goto put_channel;
  }

  if( flags & IPC_BLOCKED_ACCESS ) {
    msg=ipc_setup_task_port_message(caller,port,snd_buf,snd_size,
                                    rcv_buf,rcv_size);
  } else {
    msg=__ipc_create_nb_port_message(caller,snd_buf,snd_size);
  }

  if( !msg ) {
    r=-ENOMEM;
  } else {
    r=__ipc_port_send(port,msg,flags,rcv_buf,rcv_size);
  }

  __ipc_put_port(port);
put_channel:
  ipc_put_channel(c);
  return r;
}
