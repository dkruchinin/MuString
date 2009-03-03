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
 * eza/generic_api/exit.c: Task exit functionality.
 */

#include <eza/arch/types.h>
#include <kernel/syscalls.h>
#include <eza/smp.h>
#include <eza/process.h>
#include <eza/kernel.h>
#include <eza/task.h>
#include <ipc/ipc.h>
#include <eza/security.h>
#include <eza/process.h>
#include <ipc/port.h>
#include <eza/signal.h>
#include <eza/event.h>
#include <eza/wait.h>
#include <eza/arch/spinlock.h>
#include <eza/usercopy.h>

#define __set_exiting_flag(exiter)              \
  LOCK_TASK_STRUCT((exiter));                   \
  set_task_flags((exiter),TF_EXITING);          \
  UNLOCK_TASK_STRUCT((exiter))

#define __clear_exiting_flag(exiter)            \
  LOCK_TASK_STRUCT((exiter));                   \
  clear_task_flag((exiter),TF_EXITING);          \
  UNLOCK_TASK_STRUCT((exiter))

static void __exit_ipc(task_t *exiter) {
  task_ipc_t *ipc;
  task_ipc_priv_t *p;  

  if( exiter->ipc ) {
    close_ipc_resources(exiter->ipc);
  }

  LOCK_TASK_MEMBERS(exiter);
  ipc=exiter->ipc;
  p=exiter->ipc_priv;
  exiter->ipc=NULL;
  exiter->ipc_priv=NULL;
  UNLOCK_TASK_MEMBERS(exiter);

  if( ipc ) {
    release_task_ipc(ipc);
  }

  if( p ) {
    release_task_ipc_priv(p);
  }
}

static void __exit_scheduler(task_t *exiter)
{
  sched_del_task(exiter);
}

static void __exit_limits(task_t *exiter)
{
}

static void __exit_mm(task_t *exiter)
{
}

static int __notify_disintegration_done(disintegration_descr_t *dreq,
                                        ulong_t status)
{
  int r=-EINVAL;
  disintegration_req_packet_t *p;

  if( dreq ) {
    p=ipc_message_data(dreq->msg);

    p->pid=current_task()->pid;
    p->status=status;

    r=ipc_port_send_iov(dreq->port,dreq->msg,false,NULL,0,0);
    ipc_put_port(dreq->port);
    memfree(dreq);
  }
  return r > 0 ? 0 : r;
}

static void __flush_pending_uworks(task_t *exiter)
{
  disintegration_descr_t *dreq=exiter->uworks_data.disintegration_descr;

  if( dreq ) { /* Pending disintegration requests ? */
    __notify_disintegration_done(dreq,__DR_EXITED);
  }
}

static void __exit_resources(task_t *exiter,ulong_t flags)
{
  if( !(flags & EF_DISINTEGRATE) ) {
    /* Remove all our listeners. */
    exit_task_events(exiter);
  }
}

void do_exit(int code,ulong_t flags,ulong_t exitval)
{
  task_t *exiter=current_task();
  list_node_t *ln;
  disintegration_descr_t *dreq;

  if( !exiter->pid ) {
    panic( "do_exit(): Exiting from the idle task on CPU N%d !\n",
           cpu_id() );
  }

  if( exiter->pid == 1 && exiter->tid == 1 ) {
    panic( "do_exit(): Exiting from the Name Server on CPU N%d !\n",
           cpu_id() );
  }

  if( in_interrupt() ) {
    panic( "do_exit(): Exiting in interrupt context on CPU N%d !\n",
           cpu_id() );
  }

  /* Voila ! We're invisible now ! */
  __set_exiting_flag(exiter);

  /* Only threads and not-terminated processes become zombies. */
  if( !(flags & EF_DISINTEGRATE) || is_thread(exiter) ) {
    zombify_task(exiter);
  }

  /* Flush any pending uworks. */
  if( !(flags & EF_DISINTEGRATE) ) {
    __flush_pending_uworks(exiter);
    task_event_notify(TASK_EVENT_TERMINATION);
  }

  /* Perform general exit()-related works. */
  __exit_mm(exiter);

  /* Clear any pending extra termination requests. */
  clear_task_disintegration_request(exiter);

  if( !is_thread(exiter) ) { /* All process-related works are performed here. */
    tg_leader_private_t *priv=exiter->tg_priv;

    if( !(flags & EF_DISINTEGRATE) ) {
      __exit_limits(exiter);
      __exit_ipc(exiter);
    }

    LOCK_TASK_CHILDS(exiter);
    if( priv->num_threads ) { /* Terminate all process's threads. */
      /* We can't just initialize event counter to the number of our threads,
       * because some of our threads might be performing 'exit_thread()' and
       * might have passed the code that raises such an event - so we can sleep
       * forever. To avoid this, we will calculate the exact amount of 'valid
       * for termination' threads.
       */
      atomic_set(&priv->ce.counter,0);

      list_for_each(&exiter->threads,ln) {
        task_t *thread=container_of(ln,task_t,child_list);

        LOCK_TASK_STRUCT(thread);
        if( thread->cwaiter != __UNUSABLE_PTR ) {
          /* Take this thread into account. */
          atomic_inc(&priv->ce.counter);
          thread->cwaiter=&priv->ce;
        }
        UNLOCK_TASK_STRUCT(thread);

        set_task_disintegration_request(thread);
        activate_task(thread);
      }

      /* Prepare threads termination event. */
      event_initialize(&priv->ce.e);
      event_set_task(&priv->ce.e,exiter);
      UNLOCK_TASK_CHILDS(exiter);

      /* Wait for all out threads to terminate. */
      event_yield(&priv->ce.e);
    } else {
      /* No threads. */
      UNLOCK_TASK_CHILDS(exiter);
    }
    /* After we have terminated all our threads, we should notify our parent. */
  } else { /* All thread-related works are performed here. */
    countered_event_t *ce;

    __exit_limits(exiter);
    __exit_ipc(exiter);

    LOCK_TASK_STRUCT(exiter);
    ce=exiter->cwaiter;
    exiter->cwaiter=__UNUSABLE_PTR; /* We don't wake anybody up anymore ! */
    UNLOCK_TASK_STRUCT(exiter);

    while( !list_is_empty(&exiter->jointed) ) { /* Notify all waiting tasks. */
      ln=list_node_first(&exiter->jointed);
      jointee_t *j=container_of(ln,jointee_t,l);
      task_t *waiter=container_of(j,task_t,jointee);

      LOCK_TASK_STRUCT(waiter);
      if( j->exiter == exiter ) {
        j->exit_ptr=exitval;
        event_raise(&j->e);
      }
      list_del(ln);
      UNLOCK_TASK_STRUCT(waiter);
    }

    /* Next, notify our parent in case he needs it. */
    LOCK_TASK_CHILDS(exiter->group_leader);
    LOCK_TASK_STRUCT(exiter);
    /* Remove us from parent's list. */
    exiter->group_leader->tg_priv->num_threads--;
    list_del(&exiter->child_list);

    UNLOCK_TASK_STRUCT(exiter);
    UNLOCK_TASK_CHILDS(exiter->group_leader);

    if( ce ) {
      countered_event_raise(ce);
    }
  }

  /* Continue task termination. */
  __exit_resources(exiter,flags);

  if( !is_thread(exiter) ) {
    if( flags & EF_DISINTEGRATE ) {
      /* Prepare the final reincarnation event. */
      event_initialize_task(&exiter->reinc_event,exiter);

      if( !__notify_disintegration_done(exiter->uworks_data.disintegration_descr,0) ) {
        /* OK, folks: for now we have no attached resources and userspace.
         * So we can't return from syscall until the process that initiated our
         * disintegration has reconstructed our userspace. So sleep until it has changed
         * our state via 'SYS_PR_CTL_REINCARNATE_TASK'.
         */
        event_yield(&exiter->reinc_event);

        /* Tell the world that we can be targeted for disintegration again. */
        LOCK_TASK_STRUCT(exiter);
        exiter->uworks_data.disintegration_descr=NULL;
        UNLOCK_TASK_STRUCT(exiter);

        /* OK, reincarnation complete.
         * So leave invisible mode, recalculate pending signals and return
         * to let the new process to execute.
         */
        __clear_exiting_flag(exiter);
        update_pending_signals(exiter);
        return;
      }
      /* In case of errors just fallthrough and deattach from scheduler. */
    } else {
      /* Tricky situation: we were targeted for termination _after_ starting
       * executing logic that invokes 'do_exit()' normally - for example,
       * during executing 'sys_exit()'. In such a case pending termination
       * requests will never be acknowledged. So we perform additional check
       * even for tasks that weren't forced to terminate itself.
       */
      LOCK_TASK_STRUCT(exiter);
      dreq=exiter->uworks_data.disintegration_descr;
      exiter->uworks_data.disintegration_descr=NULL;
      UNLOCK_TASK_STRUCT(exiter);

      if( dreq ) {
        __notify_disintegration_done(dreq,__DR_EXITED);
      }
    }
  }

  /* Bye-bye task. */
  __exit_scheduler(exiter);
  panic( "do_exit(): zombie task <%d:%d> is still running !\n",
         exiter->pid, exiter->tid);
}

void sys_exit(int code)
{
  do_exit(code,0,0);
}

void sys_thread_exit(long value)
{
  do_exit(0,0,value);
}

long sys_wait_id(idtype_t idtype,id_t id,siginfo_t *siginfo,int options)
{
  return 0;
}

long sys_waitpid(pid_t pid,int *status,int options)
{
  return 0;
}

long sys_thread_wait(tid_t tid,void **value_ptr)
{
  task_t *target=pid_to_task(tid);
  task_t *caller=current_task();
  long r;

  if( !target ) {
    return -ESRCH;
  }

  if( !is_thread(target) || target->pid != caller->pid ) {
    return -EINVAL;
  }

  if( target == caller ) {
    return -EDEADLOCK;
  }

  event_initialize_task(&caller->jointee.e,caller);

  r=0;
  LOCK_TASK_STRUCT(target);
  if( target->cwaiter != __UNUSABLE_PTR ) {
    caller->jointee.exiter=target;
    list_add2tail(&target->jointed,&caller->jointee.l);
  } else {
    caller->jointee.exiter=NULL;
    r=-EINVAL;
  }
  UNLOCK_TASK_STRUCT(target);

  if( !r ) {
    event_yield(&caller->jointee.e);

    /* Check for asynchronous interruption. */
    LOCK_TASK_STRUCT(caller);
    if( task_was_interrupted(caller) ) {
      /* We take into account only 'fairplay' interruption which means
       * only interruption while we're on the waiting list. Otherwise,
       * if target thread woke us up before the signal arrived (in such
       * a situation we're not bound to it), we assume that -EINTR shouldn't
       * be returned (but all pending signals will be delivered, nevertheless).
       */
      if( list_node_is_bound(&caller->jointee.l) ) {
        list_del(&caller->jointee.l);
        r=-EINTR;
      }
    }
    caller->jointee.exiter=NULL;
    UNLOCK_TASK_STRUCT(target);
  }

  if( !r && value_ptr ) {
    r=copy_to_user(value_ptr,&caller->jointee.exit_ptr,
                   sizeof(caller->jointee.exit_ptr));
    if( r ) {
      r=-EFAULT;
    }
  }
  return r;
}
