/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * (c) Copyright 2006,2007,2008 MString Core Team <http://mstring.jarios.org>
 * (c) Copyright 2008 Michael Tsymbalyuk <mtzaurus@gmail.com>
 *
 * eza/generic_api/process.c: base system process-related functions.
 */

#include <config.h>
#include <mm/vmm.h>
#include <eza/task.h>
#include <eza/smp.h>
#include <eza/kstack.h>
#include <eza/errno.h>
#include <eza/amd64/context.h>
#include <mlibc/kprintf.h>
#include <eza/arch/scheduler.h>
#include <mlibc/types.h>
#include <eza/kernel.h>
#include <eza/arch/task.h>
#include <eza/arch/preempt.h>
#include <eza/process.h>
#include <eza/security.h>
#include <eza/arch/current.h>
#include <eza/spinlock.h>
#include <kernel/syscalls.h>
#include <eza/kconsole.h>
#include <eza/usercopy.h>
#include <ipc/port.h>
#include <ipc/ipc.h>

typedef uint32_t hash_level_t;

/* Stuff related to PID-to-task translation. */
static rw_spinlock_t pid_to_struct_locks[PID_HASH_LEVELS];
static list_head_t pid_to_struct_hash[PID_HASH_LEVELS];

/* Macros for dealing with PID-to-task hash locks.
 * _W means 'for writing', '_R' means 'for reading' */
#define LOCK_PID_HASH_LEVEL_R(l) spinlock_lock_read(&pid_to_struct_locks[l])
#define UNLOCK_PID_HASH_LEVEL_R(l) spinlock_unlock_read(&pid_to_struct_locks[l])
#define LOCK_PID_HASH_LEVEL_W(l) spinlock_lock_write(&pid_to_struct_locks[l])
#define UNLOCK_PID_HASH_LEVEL_W(l) spinlock_unlock_write(&pid_to_struct_locks[l])

void initialize_process_subsystem(void)
{
  uint32_t i;

  /* Now initialize PID-hash arrays. */
  for( i=0; i<PID_HASH_LEVELS; i++ ) {
    rw_spinlock_initialize(&pid_to_struct_locks[i]);
    list_init_head(&pid_to_struct_hash[i]);
  }
}

static hash_level_t pid_to_hash_level(pid_t pid)
{
  return (pid & PID_HASH_LEVEL_MASK);
}

void zombify_task(task_t *target)
{
  LOCK_TASK_STRUCT(target);
  target->state=TASK_STATE_ZOMBIE;
  UNLOCK_TASK_STRUCT(target);
}

void unhash_task(task_t *task)
{
  hash_level_t l = pid_to_hash_level(task->pid);

  LOCK_PID_HASH_LEVEL_W(l);
  if( list_node_is_bound(&task->pid_list) ) {
    list_del(&task->pid_list);
  }
  UNLOCK_PID_HASH_LEVEL_W(l);
}

task_t *lookup_task_thread(task_t *p,tid_t tid)
{
  list_node_t *n;
  task_t *t,*target=NULL;

  if( tid && tid < CONFIG_THREADS_PER_PROCESS ) {
    LOCK_TASK_CHILDS(p);
    list_for_each(&p->threads,n) {
      t=container_of(n,task_t,child_list);
      if(t->tid == tid) {
        grab_task_struct(t);
        target=t;
        break;
      }
    }
    UNLOCK_TASK_CHILDS(p);
  }
  return target;
}

task_t *lookup_task(pid_t pid, tid_t tid, ulong_t flags)
{
  task_t *t,*task=NULL;
  list_node_t *n;

  if( pid < NUM_PIDS ) {
    hash_level_t l = pid_to_hash_level(pid);

    LOCK_PID_HASH_LEVEL_R(l);
    list_for_each(&pid_to_struct_hash[l],n) {
      t=container_of(n,task_t,pid_list);
      if(t->pid == pid) {
        grab_task_struct(t);
        task=t;
        break;
      }
    }
    UNLOCK_PID_HASH_LEVEL_R(l);
  }

  if( task && tid ) {
    task_t *target=lookup_task_thread(task,tid);
    release_task_struct(task);
    task=target;
  }

  if( task ) {
    t=NULL;

    LOCK_TASK_STRUCT(task);
    if( task->state == TASK_STATE_ZOMBIE ) {
      if( flags & LOOKUP_ZOMBIES ) {
        t=task;
      }
    } else {
      t=task;
    }
    UNLOCK_TASK_STRUCT(task);

    if( !t ) {
      release_task_struct(task);
    }
    task=t;
  }
  return task;
}

static void __setup_common_task_attributes(task_t *target,exec_attrs_t *attrs)
{
  target->uworks_data.destructor=attrs->destructor;
}

int create_task(task_t *parent,ulong_t flags,task_privelege_t priv,
                     task_t **newtask,task_creation_attrs_t *attrs)
{
  task_t *new_task;
  int r;

  r = create_new_task(parent,flags,priv,&new_task,attrs);
  if(r == 0) {

    if( attrs ) {
      __setup_common_task_attributes(new_task,&attrs->exec_attrs);
    }

    r = arch_setup_task_context(new_task,flags,priv,parent,attrs);
    if(r == 0) {
      /* Tell the scheduler layer to take care of this task. */
      r = sched_add_task(new_task);
      if( r == 0 ) {
        /* If it is not a thread, we can add this task to the hash. */
        if( !is_thread( new_task ) ) {
          hash_level_t l = pid_to_hash_level(new_task->pid);
          LOCK_PID_HASH_LEVEL_W(l);
          list_add2tail(&pid_to_struct_hash[l],&new_task->pid_list);
          UNLOCK_PID_HASH_LEVEL_W(l);
        }

        /* If user requests to start this task immediately, do so. */
        if( attrs && attrs->task_attrs.run_immediately == __ATTR_ON ) {
          sched_change_task_state(new_task,TASK_STATE_RUNNABLE);
        }
      }
    } else {
      /* TODO: [mt] deallocate task struct properly. */
    }
  }

  if( newtask != NULL ) {
    if(r<0) {
      new_task = NULL;
    }
    *newtask = new_task;
  }

  return r;
}

static bool __check_task_exec_attrs(exec_attrs_t *ea)
{
  bool valid=true;

  if( ea->stack ) {
    valid=valid_user_address(ea->stack);
  }

  if( valid && ea->entrypoint ) {
    valid=valid_user_address(ea->entrypoint);
  }

  if( valid && ea->destructor ) {
    valid=valid_user_address(ea->destructor);
  }

  if( valid && ea->per_task_data ) {
    valid=valid_user_address(ea->per_task_data);
  }

  return valid;
}

static int __disintegrate_task(task_t *target,ulong_t pnum)
{
  ipc_gen_port_t *port;
  disintegration_descr_t *descr;
  int r;
  iovec_t iov;
  disintegration_req_packet_t drp;
  ipc_channel_t *channel;

  if( !(port=ipc_get_port(current_task(),pnum)) ) {
    return -EINVAL;
  }

  r = ipc_open_channel_raw(port, IPC_KERNEL_SIDE, &channel);
  if (r)
    goto put_port;  
  if( port->flags & IPC_BLOCKED_ACCESS ) {
    r=-EINVAL;
    ipc_unpin_channel(channel);
    goto out;
  }

  descr=memalloc(sizeof(*descr));
  if( !descr ) {
    r=-ENOMEM;
    ipc_unpin_channel(channel);
    goto out;
  }

  iov.iov_base=&drp;
  iov.iov_len=sizeof(drp);

  descr->channel=channel;
  descr->msg=ipc_create_port_message_iov_v(channel, &iov, 1, sizeof(drp), NULL, 0, NULL, NULL, 0);
  if (!descr->msg) {
    r = -ENOMEM;
    ipc_put_channel(channel);
    goto free_descr;
  }

  LOCK_TASK_STRUCT(target);
  if( !check_task_flags(target,TF_EXITING)
      && !check_task_flags(target,TF_DISINTEGRATING) ) {

    target->uworks_data.disintegration_descr=descr;
    target->terminator=current_task();
    set_task_flags(target,TF_DISINTEGRATING);

    set_task_disintegration_request(target);
    r=0;
  } else {
    r=-ESRCH;
  }
  UNLOCK_TASK_STRUCT(target);

  if( !r ) {
    r=activate_task(target);
    if( !r ) {
      return r;
    }
    /* Fallthrough in case of error. */
  }

  put_ipc_port_message(descr->msg);
  
free_descr:
  memfree(descr);
put_port:
  ipc_put_port(port);
out:
  return r;
}

static int __reincarnate_task(task_t *target,ulong_t arg)
{
  exec_attrs_t attrs;
  int r;

  /* Check if target task is a zombie. */
  LOCK_TASK_STRUCT(target);
  if( target->terminator == current_task() &&
      check_task_flags(target,TF_DISINTEGRATING) ) {
    r=0;
  } else {
    r=-EPERM;
  }
  UNLOCK_TASK_STRUCT(target);

  if( !r ) {
    if( arg == 0 || copy_from_user(&attrs,(void*)arg,sizeof(attrs)) ) {
      r=-EFAULT;
    } else {
      if( !__check_task_exec_attrs(&attrs) ) {
        r=-EINVAL;
      } else {
        __setup_common_task_attributes(target,&attrs);

        r=arch_process_context_control(target,SYS_PR_CTL_REINCARNATE_TASK,
                                       (ulong_t)&attrs);
        if( !r ) {
          /* OK, task context was restored, so we can activate the task.
           * But first make sure we're not performing concurrent reincarnation
           * requests.
           */
          LOCK_TASK_STRUCT(target);
          if( target->terminator == current_task() &&
              check_task_flags(target,TF_DISINTEGRATING) ) {
            target->terminator=NULL;
            clear_task_flag(target,TF_DISINTEGRATING);
            r=0;
          } else {
            r=-EBUSY;
          }
          UNLOCK_TASK_STRUCT(target);

          if( !r ) {
            event_raise(&target->reinc_event);
          }
        }
      }
    }
  }

  return r;
}

long do_task_control(task_t *target,ulong_t cmd, ulong_t arg)
{
  task_event_ctl_arg te_ctl;
  long r;

  switch( cmd ) {
    case SYS_PR_CTL_SET_ENTRYPOINT:
    case SYS_PR_CTL_SET_STACK:
      if( !valid_user_address(arg) ) {
        return -EFAULT;
      }
      if( target->state == TASK_STATE_JUST_BORN ) {
        r = arch_process_context_control(target,cmd,arg);
        if (likely(r == 0))
          target->ustack = arg;

        return r;
      }

      break;
    case SYS_PR_CTL_GET_ENTRYPOINT:
    case SYS_PR_CTL_GET_STACK:
      /* No arguments are acceptable for these commands. */
      if( arg == 0 && target->state == TASK_STATE_JUST_BORN ) {
        return arch_process_context_control(target,cmd,arg);
      }
      break;
    case SYS_PR_CTL_ADD_EVENT_LISTENER:
      if(copy_from_user(&te_ctl,(void *)arg,sizeof(te_ctl) ) ) {
        return -EFAULT;
      }
      return task_event_attach(target,current_task(),&te_ctl);
    case SYS_PR_CTL_DEL_EVENT_LISTENER:
      return task_event_detach(arg,current_task());
    case SYS_PR_CTL_SET_PERTASK_DATA:
      if( !valid_user_address(arg) ) {
        return -EFAULT;
      }
      r = arch_process_context_control(target,SYS_PR_CTL_SET_PERTASK_DATA,
                                       arg);
      if (likely(r == 0))
        target->ptd = arg;

      return r;
    case SYS_PR_CTL_DISINTEGRATE_TASK:
      if( target == current_task() ) {
        return -EWOULDBLOCK;
      }
      return __disintegrate_task(target,arg);
    case SYS_PR_CTL_REINCARNATE_TASK:
      return __reincarnate_task(target,arg);
    case SYS_PR_CTL_SET_CANCEL_STATE:
      if( target != current_task() ||
          (arg != PTHREAD_CANCEL_ENABLE && arg != PTHREAD_CANCEL_DISABLE) ) {
        return -EINVAL;
      }

      LOCK_TASK_STRUCT(target);
      r=target->uworks_data.cancel_state;
      target->uworks_data.cancel_state=arg;
      if( arg == PTHREAD_CANCEL_ENABLE ) {
        if( (target->uworks_data.flags & DAF_CANCELLATION_PENDING ) &&
            target->uworks_data.cancel_type == PTHREAD_CANCEL_ASYNCHRONOUS) {
          set_task_disintegration_request(current_task());
        }
      }
      UNLOCK_TASK_STRUCT(target);
      return r;
    case SYS_PR_CTL_SET_CANCEL_TYPE:
      if( target != current_task() ||
          (arg != PTHREAD_CANCEL_DEFERRED && arg != PTHREAD_CANCEL_ASYNCHRONOUS) ) {
        return -EINVAL;
      }

      LOCK_TASK_STRUCT(target);
      r=target->uworks_data.cancel_type;
      target->uworks_data.cancel_type=arg;
      if( arg == PTHREAD_CANCEL_ASYNCHRONOUS ) {
        if( (target->uworks_data.flags & DAF_CANCELLATION_PENDING) &&
            target->uworks_data.cancel_state == PTHREAD_CANCEL_ENABLE ) {
          set_task_disintegration_request(current_task());
        }
      }
      UNLOCK_TASK_STRUCT(target);
      return r;
    case SYS_PR_CTL_CANCEL_TASK:
      if( target->pid != current_task()->pid ) {
        return -ESRCH;
      }
      LOCK_TASK_STRUCT(target);
      if( !(target->uworks_data.flags & DAF_CANCELLATION_PENDING) ) {
        target->uworks_data.flags |= DAF_CANCELLATION_PENDING;

        if( target->uworks_data.cancel_state == PTHREAD_CANCEL_ENABLE ) {
          ulong_t mask;

          if( target->uworks_data.cancel_type == PTHREAD_CANCEL_ASYNCHRONOUS ) {
            mask=TASK_STATE_RUNNABLE | TASK_STATE_RUNNING | TASK_STATE_SLEEPING;
          } else {
            mask=TASK_STATE_SLEEPING;
          }

          set_task_disintegration_request(target);
          sched_change_task_state_mask(target,TASK_STATE_RUNNABLE,mask);
        }
      }
      UNLOCK_TASK_STRUCT(target);
      return 0;
  }
  return -EINVAL;
}

void force_task_exit(task_t *target,int exit_value)
{
  LOCK_TASK_STRUCT(target);
  if( !(target->flags & TF_EXITING) && !(target->uworks_data.flags & DAF_EXIT_PENDING) ) {
    target->uworks_data.flags |= DAF_EXIT_PENDING;
    set_task_disintegration_request(target);
    target->uworks_data.exit_value=exit_value;
    sched_change_task_state_mask(target,TASK_STATE_RUNNABLE,TASK_STATE_SLEEPING);
  }
  UNLOCK_TASK_STRUCT(target);
}

int sys_task_control(pid_t pid, tid_t tid, ulong_t cmd, ulong_t arg)
{
  int r;
  task_t *task;
  ulong_t lookup_flags=0;
  task_t *caller=current_task();

  if( tid ) {
    switch( cmd ) {
      case SYS_PR_CTL_DISINTEGRATE_TASK:
        kprintf_fault("[1]\n");
        return -EINVAL;
    }
  }

  /* First check for commands suitable only for the caller as the target. */
  switch( cmd ) {
    case SYS_PR_CTL_GETPID:
      if( !pid ) {
        return caller->pid;
      }
      return -EINVAL;

    case SYS_PR_CTL_GETPPID:
      if( !pid ) {
        return caller->ppid;
      }
      return -EINVAL;

    case SYS_PR_CTL_GETTID:
      if( !pid ) {
        return caller->tid;
      }
      return -EINVAL;
  }

  /* Only reincarnation can target zombies. */
  if( cmd == SYS_PR_CTL_REINCARNATE_TASK ) {
    lookup_flags |= LOOKUP_ZOMBIES;
  }

  if( !pid ) {
    pid=current_task()->pid;
  }

  if( !(task=lookup_task(pid,tid,lookup_flags)) ) {
    return -ESRCH;
  }

  if( !security_ops->check_process_control(task,cmd,arg) ) {
    r = -EACCES;
    goto out_release;
  }

  r = do_task_control(task,cmd,arg);
out_release:
  release_task_struct(task);
  return r;
}

long sys_create_task(ulong_t flags,task_creation_attrs_t *a)
{
  task_t *task;
  long r;
  task_creation_attrs_t attrs,*pa;

  if( !security_ops->check_create_process(flags) ) {
    return -EPERM;
  }

  if( a ) {
    if( copy_from_user(&attrs,a,sizeof(attrs) ) ) {
      return -EFAULT;
    }
    if( !__check_task_exec_attrs(&attrs.exec_attrs) ) {
      return -EINVAL;
    }
    pa=&attrs;
  } else {
    pa=NULL;
  }

  r = create_task(current_task(), flags, TPL_USER, &task,pa);
  if( !r ) {
    if( is_thread(task) ) {
      r=task->tid;
    } else {
      r=task->pid;
    }
  }

  return r;
}

extern ulong_t syscall_counter;

long sys_fork(void)
{
  task_t *new,*caller=current_task();
  long r;
  task_creation_attrs_t tca;

  memset(&tca,0,sizeof(tca));

  tca.exec_attrs.stack=0;
  tca.exec_attrs.destructor=caller->uworks_data.destructor;
  tca.exec_attrs.per_task_data=caller->ptd;

  r=create_task(current_task(),CLONE_COW | CLONE_REPL_IPC,
                TPL_USER,&new,&tca);
  if( !r ) {
    r=new->pid;
  }
  return r;
}

