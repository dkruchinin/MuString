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
 * (c) Copyright 2009,2010 Dmitry Gromada <gromada@jarios.org>
 *
 * mstring/generic_api/ptrace.c: process trace functionality.
 */

#include <arch/types.h>
#include <mstring/ptrace.h>
#include <mstring/errno.h>
#include <mstring/process.h>
#include <security/security.h>
#include <arch/ptrace.h>
#include <ds/list.h>
#include <sync/spinlock.h>
#include <mstring/usercopy.h>
#include <mstring/signal.h>
#include <mm/vmm.h>
#include <mm/mem.h>

static inline bool is_attach_request(ptrace_cmd_t cmd)
{
  return (cmd == PTRACE_ATTACH || PTRACE_TRACEME);
}

static bool deferred_continue(void *data)
{
  task_t *t = (task_t*)data;

  return (t->state == TASK_STATE_STOPPED);
}

static bool check_task_state(ptrace_cmd_t cmd, task_t *task)
{
  bool ret;

  if (task->state == TASK_STATE_ZOMBIE)
    return false;

  if (cmd == PTRACE_ADD_TRACED_EVENT ||
      cmd == PTRACE_REMOVE_TRACED_EVENT) {
    return true;
  }

  ret = is_attach_request(cmd) ||
        (task_traced(task) && task_stopped(task));

  return ret;
}

static void __ptrace_attach(task_t *tracer, task_t *child)
{
  grab_task_struct(tracer);
  grab_task_struct(child);
  list_add2tail(&tracer->trace_children, &child->trace_list);
  child->tracer = tracer;
  process_trace_data(child).mask = 0;
  if (child->state == TASK_STATE_STOPPED)
    set_ptrace_event(child, PTRACE_EV_STOPPED);
}

static void __ptrace_detach(task_t *tracer, task_t *child)
{
  clear_task_flag(child, TF_SINGLE_STEP);
  child->tracer = NULL;
  list_del(&child->trace_list);
  set_ptrace_event(child, PTRACE_EV_NONE);
  release_task_struct(tracer);
  release_task_struct(child);
}

static int ptrace_attach(task_t *tracer, task_t *child)
{
  int ret = 0;

  if (!s_check_access(S_GET_TASK_OBJ(tracer), S_GET_TASK_OBJ(child)))
    return ERR(-EPERM);

  if (is_kernel_thread(child) || tasks_in_same_group(tracer, child))
    return ERR(-EPERM);

  LOCK_TASK_CHILDS(tracer);
  LOCK_TASK_STRUCT(child);

  if (task_traced(child)) {
    ret = ERR(-EBUSY);
  } else if (child->state == TASK_STATE_ZOMBIE ||
             check_task_flags(tracer, TF_EXITING)) {
    ret = -ESRCH;
  } else {
    __ptrace_attach(tracer, child);
  }

  UNLOCK_TASK_STRUCT(child);
  UNLOCK_TASK_CHILDS(tracer);

  if (!ret && (child->state != TASK_STATE_JUST_BORN)) {
    /*
     * now, one can stop the target process
     */

    usiginfo_t sinfo;

    sinfo.si_signo = SIGSTOP;
    send_process_siginfo(child->pid, &sinfo, NULL, child, true);
  }

  return ERR(ret);
}

static void ptrace_cont(task_t *child)
{
  sigqueue_t *squeue = &child->siginfo.sigqueue;

  // !!! DEBUG
   kprintf("[KERNEL, %s, line %d]: tid = %d\n",
           __func__, __LINE__, child->tid);

  /* clear the SIGSTOP signal if it is pending */
  LOCK_TASK_SIGNALS(child);
  if (bit_test(squeue->active_mask, SIGSTOP)) {
    sigqueue_remove_item(squeue, SIGSTOP, true);
  }
  UNLOCK_TASK_SIGNALS(child);

  LOCK_TASK_STRUCT(child);
  clear_task_flag(child, TF_INFAULT);
  set_ptrace_event(child, PTRACE_EV_NONE);
  arch_ptrace_cont(child);
  if (child->wstat == WSTAT_STOPPED)
    child->wstat = 0;

  sched_change_task_state_deferred(child, TASK_STATE_RUNNABLE,
                                   deferred_continue, child);
  UNLOCK_TASK_STRUCT(child);
}

int ptrace_detach(task_t *tracer, task_t *child, unsigned int flags)
{
  int ret = 0;

  if (!(flags & DETACH_NOLOCK))
    LOCK_TASK_CHILDS(tracer);

  LOCK_TASK_STRUCT(child);

  /*
   * Important check because the child can be detached concurrently:
   * on the other hand from the ptrace syscall, on the other hand by
   * from the exit context.
   */
  if (child->tracer != tracer) {
    ret = -EPERM;
    goto detach_complete;
  }

  /*
   * don't allow not stopped process because a debug event could be already
   * occurred for the debugged task
   */
  if (task_stopped(child) || (flags & DETACH_FORCE))
    __ptrace_detach(tracer, child);
  else
    ret = -ESRCH;

detach_complete:
  UNLOCK_TASK_STRUCT(child);
  if (!(flags & DETACH_NOLOCK))
    UNLOCK_TASK_CHILDS(tracer);

  if ( !(ret || (child->state & (TASK_STATE_JUST_BORN | TASK_STATE_ZOMBIE))) ) {
    /* resume all threads */
    task_t *t;

    LOCK_TASK_CHILDS(child);
    list_for_each_entry(&child->threads, t, child_list) {
      ptrace_cont(t);
    }
    UNLOCK_TASK_CHILDS(child);

    ptrace_cont(child);
  }

  return ERR(ret);
}

static int ptrace_memio(task_t *child, int rw, uintptr_t addr,
                        void *data)
{
  int ret;
  vmm_t *vmm = child->task_mm;
  long *va;

  /*
   * Fixme (?) [dg]: The code doesn't take into account 32-bit mm
   * related features.
   */

  /* don't allow not long word aligned memory operations */
  if ((addr & (sizeof(long) - 1)) ||
      ((long)data & (sizeof(long) - 1))) {
    return ERR(-EINVAL);
  }

  rwsem_down_read(&vmm->rwsem);
  va = (long*)user_to_kernel_vaddr(&vmm->rpd, addr);
  if (va == NULL) {
    ret = -EINVAL;
  } else {
    if (rw)
      ret = copy_from_user(va, data, sizeof(long));
    else
      ret = copy_to_user(data, va, sizeof(long));
  }
  rwsem_up_read(&vmm->rwsem);

  return ret;
}

static int ptrace_kill(task_t *child)
{
  return 0;
}

int ptrace_stop(ptrace_event_t event, ulong_t msg)
{
  task_t *child = current_task();
  bool signaled = false;

  // !!! DEBUG
  kprintf("[KERNEL, %s, line %d]: tid = %d, event = %d, msg = %lu\n",
          __func__, __LINE__, child->tid, event, msg);

  /*
   * yield CPU to a debugger
   *
   * The operation must be atomic to avoid situation when the debugger
   * checks the task state before it is set to STOPPED.
   */
  LOCK_TASK_STRUCT(child);
  if (!child->group_leader->tracer ||
      check_task_flags(child, TF_GCOLLECTED)) {

    UNLOCK_TASK_STRUCT(child);
    return ERR(-ESRCH);
  }

  set_ptrace_event(child, event);
  child->pt_data.msg = msg;
  if (!(child->wstat & WSTAT_SIGNALED)) {
    child->last_signum = SIGTRAP;   /* signal emulation for ptracer */
  } else {
    /*
     * for the time of tracing disable event pick up for everything
     * except the tracer
     */
    signaled = true;
    child->wstat = 0;
  }

  sched_change_task_state(child, TASK_STATE_STOPPED);

  /*
   * Disable preemption so that to ensure the task will be
   * running for the time of waking the tracer up
   */
  preempt_disable();

  /* check if the tread has became into the kernel due to an exception */
  if ((event == PTRACE_EV_TRAP) ||
      ((event == PTRACE_EV_EXIT) && (child->wstat & WSTAT_SIGNALED))) {
    set_task_flags(child, TF_INFAULT);
  }

  UNLOCK_TASK_STRUCT(child);

  wakeup_tracer(child);
  preempt_enable();

  /* здесь задача уже вновь запущена отладчиком */
  if (signaled)
    child->wstat = WSTAT_SIGNALED;

  return 0;
}

task_t *get_process_tracer(task_t *child)
{
  task_t *t;
  task_t *gleader = child->group_leader;

  LOCK_TASK_STRUCT(gleader);
  t = gleader->tracer;
  if (t)
    grab_task_struct(t);

  UNLOCK_TASK_STRUCT(gleader);

  return t;
}

int sys_ptrace(ptrace_cmd_t cmd, pid_t pid, tid_t tid, uintptr_t addr, void *data)
{
  int ret = 0;
  task_t *child, *tracer;

  if (!s_check_system_capability(SYS_CAP_PTRACE))
    return ERR(-EPERM);

  if (cmd == PTRACE_ATTACH || cmd == PTRACE_DETACH)
    tid = 0;    /* these commands are applicable only for whole process */

  if (cmd == PTRACE_TRACEME) {
    tracer = lookup_task(pid, 0, 0);
    child = current_task()->group_leader;
  } else {
    tracer = current_task()->group_leader;
    child = lookup_task(pid, tid, 0);
  }

  if (child == NULL || tracer == NULL || !check_task_state(cmd, child)) {
    ret = -ESRCH;
    goto out;
  }

  if (!is_attach_request(cmd) && (process_tracer(child) != tracer)) {
    ret = -EPERM;
    goto out;
  }

  switch (cmd) {
  case PTRACE_ATTACH:
  case PTRACE_TRACEME:
    ret = ptrace_attach(tracer, child);
    break;
  case PTRACE_DETACH:
    ret = ptrace_detach(tracer, child, 0);
    break;
  case PTRACE_CONT:
    if (child->state == TASK_STATE_JUST_BORN) {
      ret = -ESRCH;
    } else {
      clear_task_flag(child, TF_SINGLE_STEP);
      ptrace_cont(child);
    }

    break;
  case PTRACE_KILL:
    ret = ptrace_kill(child);
    break;
  case PTRACE_READ_MEM:
  case PTRACE_WRITE_MEM:
  {
    int rw = (cmd == PTRACE_WRITE_MEM);

    if (check_task_flags(child, TF_DISINTEGRATING))
      ret = -EPERM;
    else
      ret = ptrace_memio(child, rw, addr, data);

    break;
  }
  case PTRACE_GET_SIGINFO:
  {
    usiginfo_t sinfo, *p;

    if (child->last_siginfo == NULL) {
      p = &sinfo;
      memset(&sinfo, 0, sizeof(sinfo));
      sinfo.si_signo = child->last_signum;
    } else {
      p = (usiginfo_t*)child->last_siginfo;
    }
    ret = copy_to_user(data, p, sizeof(usiginfo_t));
    break;
  }
  case PTRACE_ADD_TRACED_EVENT:
  case PTRACE_REMOVE_TRACED_EVENT:
  {
    ptrace_event_t ev;

    ret = copy_from_user(&ev, data, sizeof(ev));
    if (!ret && ((unsigned)ev >= PTRACE_EV_LAST))
      ret = -EINVAL;

    if (!ret) {
      if (cmd == PTRACE_ADD_TRACED_EVENT)
        process_trace_data(child).mask |= 1 << ev;
      else
        process_trace_data(child).mask &= ~(1 << ev);
    }

    break;
  }
  case PTRACE_READ_EVENT_MSG:
    ret = copy_to_user(data, &child->pt_data.msg, sizeof(child->pt_data.msg));
    break;
  default:
    ret = arch_ptrace(cmd, child, addr, data);
    if (!ret && (cmd == PTRACE_SINGLE_STEP))
      ptrace_cont(child);

    break;
  }

out:
  if (child && (cmd != PTRACE_TRACEME))
    release_task_struct(child);
  if ((cmd == PTRACE_TRACEME) && tracer)
    release_task_struct(tracer);

  return ERR(ret);
}

bool ptrace_reparent(task_t *tracer, task_t *child)
{
  bool ret = true;

  LOCK_TASK_CHILDS(tracer);
  if (child->tracer == tracer && !check_task_flags(tracer, TF_EXITING)) {
    /* appoint the tracer as new parent */
    list_del(&child->child_list);
    list_add2tail(&tracer->children, &child->child_list);
    child->ppid = tracer->pid;
  } else {
    ret = false;
  }
  UNLOCK_TASK_CHILDS(tracer);

  return ret;
}
