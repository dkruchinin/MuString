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
 * (c) Copyright 2009,2010 Dmitry Gromada <gromada@jarios.org>
 *
 * mstring/generic_api/exit.c: Task exit functionality.
 */

#include <arch/types.h>
#include <kernel/syscalls.h>
#include <mstring/smp.h>
#include <mstring/process.h>
#include <mstring/panic.h>
#include <mstring/task.h>
#include <ipc/ipc.h>
#include <mstring/process.h>
#include <ipc/port.h>
#include <mstring/signal.h>
#include <mstring/event.h>
#include <mstring/wait.h>
#include <arch/spinlock.h>
#include <mstring/usercopy.h>
#include <security/security.h>
#include <security/util.h>
#include <mstring/ptrace.h>

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

typedef union __wait_value {
  int status;
  void *value_ptr;
} wait_value_t;

struct wait_info {
  wait_value_t stat;
  wait_type_t wtype;
  int options;
  usiginfo_t sinfo;
  bool copy_sinfo;
};

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

extern int __dump_all;

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

    /*
     * resume the thread if it is was stopped
     * due to some traced event
     */
    if ((thread->state == TASK_STATE_STOPPED) &&
        (ptrace_event(thread) != PTRACE_EV_NONE)) {
      usiginfo_t sinfo;

      sinfo.si_signo = SIGCONT;
      send_task_siginfo(thread, &sinfo, false, NULL, exiter);
    }

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

static int __wakeup_waiters(list_head_t *jlist, task_t *task, bool tracer_only)
{
  int cnt = 0;
  list_node_t *node, *save;
  jointee_t *j;
  task_t *waiter;

  list_for_each_safe(jlist, node, save) {
    j = container_of(node, jointee_t, l);
    waiter = container_of(j, task_t, jointee);
    if (!tracer_only || (waiter->group_leader == process_tracer(task))) {
      /* check if the waiter waits for the task */
      if ((j->exiter == task) ||
          (j->type == WAIT_GROUP && j->exiter == task->group_leader) ||
          (j->type == WAIT_ANY && !task->tid)) {
        list_del(&j->l);
        event_raise(&j->e);
        cnt++;
      }
    }

    if (tracer_only && cnt)
      break;
  }

  return cnt;
}

static void do_wakeup(task_t *task, bool tracer_only)
{
  countered_event_t *ce = NULL;
  task_t *parent, *tracer;
  int wake_cnt = 0;

  if (task->state == TASK_STATE_ZOMBIE) {
    if( is_thread(task) ) {
      LOCK_TASK_STRUCT(task);
      ce = task->cwaiter;
      task->cwaiter=__UNUSABLE_PTR;
      UNLOCK_TASK_STRUCT(task);
    } else {
      ce=NULL; //exiter->cwaiter;
      task->cwaiter=__UNUSABLE_PTR;
    }
  }

  /* let's wakeup waiters */

  /* at first try to wakeup a debugger if it is */
  tracer = get_process_tracer(task);
  if (tracer) {
    LOCK_TASK_CHILDS(tracer);
    wake_cnt = __wakeup_waiters(&tracer->jointed, task, tracer_only);
    UNLOCK_TASK_CHILDS(tracer);
    release_task_struct(tracer);
  }

  /* wakeup another waiters */
  if (!(tracer_only && wake_cnt)) {
    parent = pid_to_task(task->ppid);
    if (parent) {
      LOCK_TASK_CHILDS(parent);
      __wakeup_waiters(&parent->jointed, task, tracer_only);
      UNLOCK_TASK_CHILDS(parent);
      release_task_struct(parent);
    }
  }

  if( ce ) {
    countered_event_raise(ce);
  }
}

static void __unlink_children(task_t *exiter)
{
  task_t *child, *tracer;
  list_node_t *node, *save;
  bool reap;

  list_for_each_safe(&exiter->children, node, save) {
    child = container_of(node, task_t, child_list);
    reap = false;
    tracer = get_process_tracer(exiter);
    if (tracer) {
      if (exiter != tracer)
        reap = !ptrace_reparent(tracer, child);

      release_task_struct(tracer);
    }

    if (reap)
      child->ppid=CHILD_REAPER_PID;
  }
}

static void __detach_tracings(task_t *exiter)
{
  task_t *child;
  list_node_t *node, *save;

  /*
   * spinlock holding is critical because someone can want to
   * concurrently call 'PTRACEME'. List lookup must be safe because
   * of possibility of all thread group detaching
   */
  LOCK_TASK_CHILDS(exiter);
  list_for_each_safe(&exiter->trace_children, node, save) {
    child = container_of(node, task_t, trace_list);
    ptrace_detach(exiter, child, DETACH_FORCE | DETACH_NOLOCK);
  }
  UNLOCK_TASK_CHILDS(exiter);
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

  if (event_is_traced(exiter, PTRACE_EV_EXIT) && !(flags & EF_DISINTEGRATE))
    ptrace_stop(PTRACE_EV_EXIT, 0);

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

        if (event_is_traced(exiter, PTRACE_EV_EXEC))
          ptrace_stop(PTRACE_EV_EXEC, 0);

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
    __detach_tracings(exiter);
    __unlink_children(exiter);
    __notify_parent(exiter);
  } else { /* is_thread(). */
    __exit_limits(exiter);
    __exit_ipc(exiter);
    __exit_resources(exiter,flags);
  }

  zombify_task(exiter);
  exiter->exit_val = exitval;
  if (!(exiter->wstat & WSTAT_SIGNALED))
    exiter->wstat = WSTAT_EXITED;

  wakeup_waiters(exiter);
  LOCK_TASK_STRUCT(exiter);
  exiter->last_siginfo = NULL;
  UNLOCK_TASK_STRUCT(exiter);

  sched_del_task(exiter);
  panic( "do_exit(): zombie task <%d:%d> is still running !\n",
         exiter->pid, exiter->tid);
}

/*
 * Orphan the child or reparent if it is traced (the tracer will be new parent)
 * NOTE: if the child is a thread, the function is always called
 *       under hold child spinlock of the group leader.
 */
static bool try_orphan_child(task_t *child, bool tlock)
{
  bool r = false;
  task_t *tracer;
  unsigned int flags = DETACH_FORCE;

  if (child->state != TASK_STATE_ZOMBIE || !list_node_is_bound(&child->child_list))
    return false;

  if (!is_thread(child)) {
    tracer = get_process_tracer(child);
    if (tracer != NULL) {
      if (current_task()->group_leader == tracer) {
        /* release links taken at process attachment */
        if (!tlock || (tracer->pid == child->ppid))
          flags |= DETACH_NOLOCK;

        ptrace_detach(tracer, child, flags);
      } else if (tracer->pid != child->ppid) {
        r = !ptrace_reparent(tracer, child);
      }

      /* orphan if the tracer is already the parent of the task */
      if (tracer->pid == child->ppid)
        r = true;

      release_task_struct(tracer);
    }
  } else {
    r = true;
  }

  if (r) {
    if (is_thread(child)) {
      LOCK_TASK_STRUCT(child);
      child->group_leader->tg_priv->num_threads--;
      UNLOCK_TASK_STRUCT(child);
    }
    list_del(&child->child_list);
  }

  return r;
}

static bool dump_pt_event(task_t *caller, task_t *child, int *status)
{
  bool ret = false;

  if (ptrace_event(child) != PTRACE_EV_NONE &&
      caller->group_leader == process_tracer(child)) {
    *status = ptrace_status(child);
    ret = true;
  }

  return ret;
}

static void check_dump_siginfo(struct wait_info *winfo, task_t *task)
{
  if (winfo->copy_sinfo) {
    if (task->last_siginfo) {
      memcpy(&winfo->sinfo, task->last_siginfo, sizeof(usiginfo_t));
    } else {
      memset(&winfo->sinfo, 0, sizeof(usiginfo_t));
      winfo->sinfo.si_signo = task->last_signum;
    }
  }
}

static bool status_is_waitable(wait_status_t wstat, int options)
{
  if ((options & WEXITED) && (wstat & (WSTAT_EXITED | WSTAT_SIGNALED)))
    return true;
  else if ((options & WSTOPPED) && (wstat & WSTAT_STOPPED))
    return true;
  else if ((options & WCONTINUED) && (wstat & WSTAT_CONTINUED))
    return true;

  return false;
}

static int __wait_task(task_t *target, task_t *parent, struct wait_info *winfo)
{
  int r = 1;
  task_t *caller=current_task();
  bool unref=false;
  bool unlock = true;
  wait_status_t wstat;
  int options = winfo->options;
  bool isthread = is_thread(target);

  LOCK_TASK_STRUCT(target);
  if (!dump_pt_event(caller, target, &winfo->stat.status)) {
    if( target->cwaiter == __UNUSABLE_PTR &&
        !(isthread && check_task_flags(target, TF_GCOLLECTED))) {
      check_dump_siginfo(winfo, target);
      UNLOCK_TASK_STRUCT(target);
      unlock = false;
      if (!(options & WNOWAIT)) {
        unref = try_orphan_child(target, winfo->wtype != WAIT_ANY);
        if (!unref)
          options |= WNOWAIT;
      }
    }

    wstat = target->wstat;
    if (status_is_waitable(wstat, winfo->options)) {
      if (wstat & WSTAT_EXITED) {
        if (isthread && (winfo->wtype == WAIT_SINGLE))  /* it's waiting for the thread only? */
          winfo->stat.value_ptr = (void*)target->exit_val;
        else
          winfo->stat.status = wstat | ((target->exit_val & 0xFF) << 8);
      } else {
        winfo->stat.status = wstat | (target->last_signum << 8);
      }
      if (!(options & WNOWAIT))
        target->wstat = 0;
    } else {
      r = 0;
    }
  } else if (!(options & WNOWAIT)) {
    set_ptrace_event(target, PTRACE_EV_NONE);
    if (target->wstat & WSTAT_STOPPED)
      target->wstat = 0;
  }

  if (unlock) {
    check_dump_siginfo(winfo, target);
    UNLOCK_TASK_STRUCT(target);
  }

   if( unref ) { /* Remove task's parent reference. */
    if (!isthread)
      unhash_task(target);

    release_task_struct(target);
  }

  return r;
}

static long do_wait(task_t *task, wait_value_t *stat, int options, usiginfo_t *sinfo,
                    wait_type_t type)
{
  list_head_t *head = NULL;
  task_t *target = NULL, *parent, *caller, *gleader = NULL;
  list_node_t *node, *save;
  bool trace_pass;
  long ret;
  struct wait_info winfo;

  caller = current_task();

  winfo.options = options;
  winfo.copy_sinfo = (sinfo != NULL);
  winfo.wtype = type;

  if (type == WAIT_GROUP || (task && is_thread(task))) {
    LOCK_TASK_STRUCT(task);
    gleader = task->group_leader;
    grab_task_struct(gleader);
    UNLOCK_TASK_STRUCT(task);
  }

repeat:
  ret = 0;
  gleader = NULL;
  trace_pass = false;

  if (type == WAIT_ANY) {
    /*
     * Fixme [dg]: use group leader instead of caller so that to have
     *    possibility to wait for any child from any thread of the
     *    process
     */
    parent = caller;
    head = &parent->children;
  } else {
    parent = pid_to_task(task->ppid);
    if (!parent) {
      ret = -ESRCH;
      goto out;
    }
    if (type == WAIT_GROUP)
      head = &task->threads;

    target = task;
  }

  LOCK_TASK_CHILDS(parent);
  if (!((caller->pid == parent->pid) ||
        (caller->group_leader == process_tracer(task)) ||
     tasks_in_same_group(caller, task))) {
    ret = -ESRCH;
    goto unlock_parent;
  }

  if ((type == WAIT_ANY) && list_is_empty(&parent->children) &&
      list_is_empty(&parent->trace_children)) {
    ret = -ESRCH;
    goto unlock_parent;
  }

  if (gleader)
    LOCK_TASK_CHILDS(gleader);

  if (target) {
    ASSERT(caller->sobject != NULL && target->sobject != NULL);
    if (tasks_in_same_group(caller, target) ||
        s_check_access(S_GET_TASK_OBJ(caller), S_GET_TASK_OBJ(target))) {

      ret = __wait_task(target, parent, &winfo);
    } else {
      ret = -EPERM;
    }
  }

  /* wait cycle for children or threads */
  while (head && !ret) {
    list_for_each_safe(head, node, save) {
      if (trace_pass)
        target = container_of(node, task_t, trace_list);
      else
        target = container_of(node, task_t, child_list);

      if (tasks_in_same_group(caller, target) ||
          s_check_access(S_GET_TASK_OBJ(caller), S_GET_TASK_OBJ(target))) {

        ret = __wait_task(target, parent, &winfo);
      } else {
        ret = -EPERM;
      }
      if (ret)
        break;
    }

    if (!ret && type == WAIT_ANY && !trace_pass) {
      head = &parent->trace_children;
      trace_pass = true;
    } else {
      head = NULL;
    }
  }

  if (gleader)
    UNLOCK_TASK_CHILDS(gleader);

  if (!(ret || (options & WNOHANG))) {
    if ((type != WAIT_ANY) && (task->state == TASK_STATE_ZOMBIE)) {
      ret = -ESRCH;   /* the task is already collected */
    } else {
      event_initialize_task(&caller->jointee.e, caller);
      caller->jointee.type = type;
      caller->jointee.exiter = task;
      if (task && (caller->group_leader == process_tracer(task)))
        list_add2head(&parent->jointed, &caller->jointee.l);
      else
        list_add2tail(&parent->jointed, &caller->jointee.l);
    }
  }

unlock_parent:
  UNLOCK_TASK_CHILDS(parent);

  if (!(ret || (options & WNOHANG))) {
    event_yield(&caller->jointee.e);

    if( task_was_interrupted(caller) ) {
      /* We take into account only 'fairplay' interruption which means
       * only interruption while we're on the waiting list. Otherwise,
       * if target thread woke us up before the signal arrived (in such
       * a situation we're not bound to it), we assume that -EINTR shouldn't
       * be returned (but all pending signals will be delivered, nevertheless).
       */
      LOCK_TASK_CHILDS(parent);
      if( list_node_is_bound(&caller->jointee.l) ) {
        list_del(&caller->jointee.l);
      }
      UNLOCK_TASK_CHILDS(parent);

      ret = -EINTR;
    } else {
      if (type != WAIT_ANY)
        release_task_struct(parent);

      goto repeat;
    }
  } else if (ret > 0) {
    if (stat)
      ret = put_user(winfo.stat.status, stat);
    if (!ret && sinfo)
      ret = copy_to_user(sinfo, &winfo.sinfo, sizeof(usiginfo_t));
    if (!ret)
      ret = (target->tid || (type == WAIT_GROUP)) ? target->tid : target->pid;
  }

out:
  if (gleader)
    release_task_struct(gleader);
  if ((type != WAIT_ANY) && parent)
    release_task_struct(parent);

  return ERR(ret);
}

void wakeup_waiters(task_t *task)
{
  do_wakeup(task, false);
}

void wakeup_tracer(task_t *task)
{
  do_wakeup(task, true);
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
  return ERR(-ENOSYS);
}

long sys_thread_wait(tid_t tid, void **value_ptr)
{
  task_t *target, *caller = current_task();
  long r;

  if( !tid || tid == caller->tid ) {
    return ERR(-EINVAL);
  }

  if( !(target = lookup_task(caller->pid, tid, LOOKUP_ZOMBIES)) )
    return ERR(-ESRCH);

  r = do_wait(target, (wait_value_t*)value_ptr, WEXITED, NULL, WAIT_SINGLE);
  release_task_struct(target);  /* Initial parent reference. */

  return ERR(r);
}

long sys_waitpid(pid_t pid,int *status,int options)
{
  task_t *target = NULL,*caller=current_task();
  long r;
  wait_type_t wtype;
  bool release = true;

  if( (pid == caller->pid) || (pid == 1) )
    return ERR(-EINVAL);

  if (options & ~(WNOHANG | WSTOPPED | WCONTINUED | WNOWAIT))
    return ERR(-EINVAL);

  if ((pid > 0) || (pid < -1)) {
    if (pid < -1) {
      /*
       * wait for any thread belonging to the process with ID equal
       * to negated 'pid'
       */
      pid = -pid;
      wtype = WAIT_GROUP;
    } else {
      wtype = WAIT_SINGLE;
    }

    target = lookup_task(pid,0,LOOKUP_ZOMBIES);
    if( !target ) {
      return ERR(-ESRCH);
    }
  } else if (pid == -1) {
    /* wait for any child */
    target = NULL;
    release = false;
    wtype = WAIT_ANY;
  } else {
    /* wait for any thread belonging to the caller's group */
    release = false;
    wtype = WAIT_GROUP;
    target = caller;
  }

  r = do_wait(target, (wait_value_t*)status, options | WEXITED, NULL, wtype);

  if (release)
    release_task_struct(target);

  return ERR(r);
}
