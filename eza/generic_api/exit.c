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
  {                                             \
    LOCK_TASK_STRUCT((exiter));                 \
    set_task_flags((exiter),TF_EXITING);        \
    UNLOCK_TASK_STRUCT((exiter));               \
  }

#define __clear_exiting_flag(exiter)            \
  {                                             \
    LOCK_TASK_STRUCT((exiter));                 \
    clear_task_flag((exiter),TF_EXITING);       \
    UNLOCK_TASK_STRUCT((exiter));               \
  }

/* Internal flags related to 'wait()' functions.
 */
#define __WT_ONE_WAITER  0x1  /*Only one waiter is allowed */

static void __exit_ipc(task_t *exiter) {
  task_ipc_t *ipc;
  task_ipc_priv_t *p;  

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

static void __exit_limits(task_t *exiter)
{
}

static int __notify_disintegration_done(disintegration_descr_t *dreq,
                                        ulong_t status)
{
  int r=-EINVAL;
  disintegration_req_packet_t *p;
  ipc_gen_port_t *port;

  if( dreq ) {
    p=ipc_message_data(dreq->msg);

    p->pid=current_task()->pid;
    p->status=status;

    r = ipc_get_channel_port(dreq->channel, &port);
    if (r)
      goto out;

    r=ipc_port_send_iov_core(port,dreq->msg,false,NULL,0,0);
    ipc_put_port(port);
    ipc_put_channel(dreq->channel);
    memfree(dreq);
  }

  out:
  return (r > 0) ? 0 : r;
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
    exit_task_events(exiter);
  }
}

static void __kill_all_threads(task_t *exiter)
{
  list_head_t zthreads;
  list_node_t *ln;
  tg_leader_private_t *priv=exiter->tg_priv;
  task_t *thread;
  bool gc;

  atomic_set(&priv->ce.counter,0);
  event_initialize_task(&priv->ce.e,exiter);
  list_init_head(&zthreads);

  /* Stage 1: initiate thread shutdown. */
  mutex_lock(&priv->thread_mutex);
repeat:
  LOCK_TASK_CHILDS(exiter);
  if( !list_is_empty(&exiter->threads) ) {
    ln=list_node_first(&exiter->threads);
    thread=container_of(ln,task_t,child_list);

    LOCK_TASK_STRUCT(thread);
    thread->flags |= TF_GCOLLECTED;
    list_del(ln);
    if( thread->cwaiter == __UNUSABLE_PTR ) {
      priv->num_threads--;
      gc=true;
    } else {
      atomic_inc(&priv->ce.counter);
      thread->cwaiter=&priv->ce;
      list_add2tail(&zthreads,ln);
      gc=false;
    }
    UNLOCK_TASK_STRUCT(thread);
    UNLOCK_TASK_CHILDS(exiter);

    if( gc ) {
      release_task_struct(thread);
    } else {
      force_task_exit(thread,0);
    }
    goto repeat;
  } else {
    UNLOCK_TASK_CHILDS(exiter);
  }
  mutex_unlock(&priv->thread_mutex);

  /* Stage 2: handle zombies. */
  if( !list_is_empty(&zthreads) ) {
    event_yield_susp(&priv->ce.e);
    list_for_each(&zthreads,ln) {
      thread=container_of(ln,task_t,child_list);
      release_task_struct(thread);
    }
  }
}

static void __wakeup_waiters(task_t *exiter,long exitval)
{
  countered_event_t *ce;

  if( is_thread(exiter) ) {
    mutex_lock(&exiter->group_leader->tg_priv->thread_mutex);
    LOCK_TASK_STRUCT(exiter);
    exiter->jointee.exit_ptr=exitval;
    ce=exiter->cwaiter;
    exiter->cwaiter=__UNUSABLE_PTR;
    UNLOCK_TASK_STRUCT(exiter);
    mutex_unlock(&exiter->group_leader->tg_priv->thread_mutex);
  } else {
    LOCK_TASK_STRUCT(exiter);
    exiter->jointee.exit_ptr=exitval;
    ce=NULL; //exiter->cwaiter;
    exiter->cwaiter=__UNUSABLE_PTR;
    UNLOCK_TASK_STRUCT(exiter);
  }

  if( !list_is_empty(&exiter->jointed) ) { /* Notify all waiting tasks. */
    jointee_t *j=container_of(list_node_first(&exiter->jointed),jointee_t,l);
    task_t *waiter=container_of(j,task_t,jointee);

    LOCK_TASK_STRUCT(waiter);
    if( j->exiter == exiter ) {
      j->exit_ptr=exitval;
      event_raise(&j->e);
    }
    list_del(&j->l);
    UNLOCK_TASK_STRUCT(waiter);
  }

  if( ce ) {
    countered_event_raise(ce);
  }
}

static void __unlink_children(task_t *exiter)
{
  task_t *child;

  list_for_each_entry(&exiter->children,child,child_list) {
    LOCK_TASK_STRUCT(child);
    child->ppid=CHILD_REAPER_PID;
    UNLOCK_TASK_STRUCT(child);
  }
}

static void __notify_parent(task_t *exiter)
{
#ifdef CONFIG_AUTOREMOVE_ORPHANS
   if( exiter->ppid == 1 ) {
     unhash_task(exiter);
   }
#else
#endif
}

void do_exit(int code,ulong_t flags,long exitval)
{
  task_t *exiter=current_task();
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

  __set_exiting_flag(exiter);

  if( !is_thread(exiter) ) { /* All process-related works are performed here. */
    __kill_all_threads(exiter);

    if( !(flags & EF_DISINTEGRATE) ) {
      if (!is_kernel_thread(exiter)) {
        vmm_destroy(exiter->task_mm);
      }

      __flush_pending_uworks(exiter);
      __exit_limits(exiter);
      __exit_ipc(exiter);
      task_event_notify(TASK_EVENT_TERMINATION);
    }

    if( flags & EF_DISINTEGRATE ) {
      /* Prepare the final reincarnation event. */
      __clear_vmranges_tree(exiter->task_mm);
      event_initialize_task(&exiter->reinc_event,exiter);
      clear_task_disintegration_request(exiter);

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
    __exit_resources(exiter,flags);
    __unlink_children(exiter);
    __notify_parent(exiter);
  } else { /* is_thread(). */
    __exit_limits(exiter);
    __exit_ipc(exiter);
    __exit_resources(exiter,flags);
  }

  zombify_task(exiter);
  __wakeup_waiters(exiter,exitval);
  sched_del_task(exiter);
  panic( "do_exit(): zombie task <%d:%d> is still running !\n",
         exiter->pid, exiter->tid);
}

void sys_exit(int code)
{
  code=EXITCODE(code,code);

  if( is_thread(current_task() ) ) {
    /* Initiate termination of the root thread and proceed. */
    force_task_exit(current_task()->group_leader,code);
  }
  do_exit(code,0,code);
}

void sys_thread_exit(long value)
{
  do_exit(0,0,value);
}

long sys_wait_id(idtype_t idtype,id_t id,usiginfo_t *siginfo,int options)
{
  return 0;
}

long sys_thread_wait(tid_t tid,void **value_ptr)
{
  task_t *target,*caller=current_task();
  task_t *tgleader;
  long r,exitval;

  if( !tid || tid == caller->tid ) {
    return -EINVAL;
  }

  if( !(target=lookup_task(current_task()->pid,tid,LOOKUP_ZOMBIES)) ) {
    return -ESRCH;
  }

  r=0;
  event_initialize_task(&caller->jointee.e,caller);

  LOCK_TASK_STRUCT(target);
  tgleader=target->group_leader;

  if( target->cwaiter != __UNUSABLE_PTR ) {
    /* Only one waiter is available. */
    if( list_is_empty( &target->jointed ) ) {
      caller->jointee.exiter=target;
      list_add2tail(&target->jointed,&caller->jointee.l);
    } else {
      r=-EBUSY;
    }
    UNLOCK_TASK_STRUCT(target);
  } else {
    /* Target task has probably exited. So try to pick up its exit pointer
     * on a different manner.
     */
    UNLOCK_TASK_STRUCT(target);

    LOCK_TASK_CHILDS(tgleader);
    LOCK_TASK_STRUCT(target);

    if( list_node_is_bound(&target->child_list) &&
        !(target->flags & TF_GCOLLECTED) ) {
      tgleader->tg_priv->num_threads--;
      list_del(&target->child_list);
      exitval=target->jointee.exit_ptr;
    } else {
      r=-EBUSY;
    }

    UNLOCK_TASK_STRUCT(target);
    UNLOCK_TASK_CHILDS(tgleader);

    if( !r ) {
      goto out_copy;
    } else {
      goto out_release;
    }
  }

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
    } else {
      exitval=caller->jointee.exit_ptr;
    }
    caller->jointee.exiter=NULL;
    UNLOCK_TASK_STRUCT(caller);

    /* Remove target thread from the list. */
    LOCK_TASK_CHILDS(tgleader);
    LOCK_TASK_STRUCT(target);
    if( list_node_is_bound(&target->child_list) &&
        !(target->flags & TF_GCOLLECTED) ) {
      tgleader->tg_priv->num_threads--;
      list_del(&target->child_list);
    }
    UNLOCK_TASK_STRUCT(target);
    UNLOCK_TASK_CHILDS(tgleader);
  }

out_copy:
  if( !r ) {
    release_task_struct(target);  /* Initial parent reference. */
    if( value_ptr ) {
      r=copy_to_user(value_ptr,&exitval,sizeof(exitval));
      if( r ) {
        r=-EFAULT;
      }
    }
  }

out_release:
  release_task_struct(target);
  return r;
}

static long __wait_task(task_t *target,int *status,int options)
{
  long r=0;
  task_t *caller=current_task();
  int exitval;
  bool unref=false;
  task_t *parent=pid_to_task(target->ppid);

  if( !parent ) {
    return -ESRCH;
  }

  event_initialize_task(&caller->jointee.e,caller);

  LOCK_TASK_STRUCT(target);
  if( target->cwaiter != __UNUSABLE_PTR ) {
    if( options & WNOHANG ) {
      UNLOCK_TASK_STRUCT(target);
      goto out;
    }
    caller->jointee.exiter=target;
    list_add2tail(&target->jointed,&caller->jointee.l);
    UNLOCK_TASK_STRUCT(target);
  } else {
    /* Target task has probably exited. So try to pick up its exit pointer
     * on a different manner.
     */
    UNLOCK_TASK_STRUCT(target);

    LOCK_TASK_CHILDS(parent);
    LOCK_TASK_STRUCT(target);

    if( list_node_is_bound(&target->child_list) ) {
      list_del(&target->child_list);
      exitval=target->jointee.exit_ptr;
      unref=true;
    } else {
      r=-ESRCH;
    }

    UNLOCK_TASK_STRUCT(target);
    UNLOCK_TASK_CHILDS(parent);
    goto found;
  }

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
    } else {
      exitval=caller->jointee.exit_ptr;
    }
    caller->jointee.exiter=NULL;
    UNLOCK_TASK_STRUCT(caller);

    /* Remove target task from the list of its parent. */
    LOCK_TASK_CHILDS(parent);
    LOCK_TASK_STRUCT(target);
    if( list_node_is_bound(&target->child_list) ) {
      list_del(&target->child_list);
      unref=true;
    }
    UNLOCK_TASK_STRUCT(target);
    UNLOCK_TASK_CHILDS(parent);
  }

found:
  if( !r && status ) {
    r=copy_to_user(status,&exitval,sizeof(exitval));
    if( r ) {
      r=-EFAULT;
    }
  }

out:
  if( unref ) { /* Remove task's parent reference. */
    unhash_task(target);
    release_task_struct(target);
  }
  release_task_struct(parent);
  return r;
}

long sys_waitpid(pid_t pid,int *status,int options)
{
  task_t *target,*caller=current_task();
  long r;

  if( pid <= 0 ) {
    return -EINVAL;
  }

  if( pid == caller->pid ) {
    return -EINVAL;
  }

  target=lookup_task(pid,0,LOOKUP_ZOMBIES);
  if( !target ) {
    return -ESRCH;
  }

  r=__wait_task(target,status,options);
  release_task_struct(target);
  return r;
}

