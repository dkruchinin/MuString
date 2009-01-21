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
 * (c) Copyright 2006,2007,2008 MString Core Team <http://mstring.berlios.de>
 * (c) Copyright 2008 Michael Tsymbalyuk <mtzaurus@gmail.com>
 *
 * eza/generic_api/signal.c: generic code of kernel signal delivery subsystem.
 */

#include <eza/arch/types.h>
#include <eza/task.h>
#include <eza/process.h>
#include <eza/errno.h>
#include <eza/arch/context.h>
#include <eza/signal.h>
#include <mm/slab.h>
#include <mm/pfalloc.h>
#include <eza/security.h>
#include <eza/usercopy.h>

static memcache_t *sigq_cache;

#define __alloc_sigqueue_item()  alloc_from_memcache(sigq_cache)

struct __def_sig_data {
  sigset_t *blocked,*pending;
  int sig;
};

static bool __deferred_sig_check(void *d)
{
  struct __def_sig_data *sd=(struct __def_sig_data *)d;
  return !signal_matches(sd->blocked,sd->sig) &&
    signal_matches(sd->pending,sd->sig);
}

void initialize_signals(void)
{
  sigq_cache = create_memcache( "Sigqueue item memcache", sizeof(sigq_item_t),
                                2, SMCF_PGEN);
  if( !sigq_cache ) {
    panic( "initialize_signals(): Can't create the sigqueue item memcache !" );
  }
}

/* NOTE: Signals must be locked before calling this function ! */
static bool __update_pending_signals(task_t *task)
{
  signal_struct_t *siginfo=&task->siginfo;
  bool delivery_needed;

  if( deliverable_signals_present(siginfo) ) {
    set_task_signals_pending(task);
    delivery_needed=true;
  } else {
    clear_task_signals_pending(task);
    delivery_needed=false;
  }
  return delivery_needed;
}

bool update_pending_signals(task_t *task)
{
  bool r;

  LOCK_TASK_SIGNALS(task);
  r=__update_pending_signals(task);
  UNLOCK_TASK_SIGNALS(task);

  return r;
}

/* NOTE: Caller must hold the signal lock !
 * Return codes:
 *   0: signal was successfully queued.
 *   1: signal wasn't queued since it was ignored.
 * -ENOMEM: no memory for a new queue item.
 */
static status_t __send_task_siginfo(task_t *task,siginfo_t *info,
                                    bool force_delivery)
{
  int sig=info->si_signo;
  status_t r;
  bool send_signal;

  if( force_delivery ) {
    sa_sigaction_t act=task->siginfo.handlers->actions[sig].a.sa_sigaction;

    if( act == SIG_IGN ) {
      task->siginfo.handlers->actions[sig].a.sa_sigaction=SIG_DFL;
    }
    send_signal=true;
  } else {
    send_signal=!signal_matches(&task->siginfo.ignored,sig);
  }

  /* Make sure only one instance of a non-RT signal is present. */
  if( !rt_signal(sig) && signal_matches(&task->siginfo.pending,sig) ) {
    return 0;
  }

  if( send_signal ) {
    sigq_item_t *qitem=__alloc_sigqueue_item();

    if( qitem ) {
      qitem->h.idx=sig;
      qitem->info=*info;

      sigqueue_add_item(&task->siginfo.sigqueue,&qitem->h);
      r=0;
    } else {
      r=-ENOMEM;
    }
  } else {
    r=1;
  }
  return r;
}

static void __send_siginfo_postlogic(task_t *task,siginfo_t *info)
{
  if( update_pending_signals(task) && task != current_task() ) {
    /* Need to wake up the receiver. */
    struct __def_sig_data sd;
    ulong_t state=TASK_STATE_SLEEPING | TASK_STATE_STOPPED;

    sd.blocked=&task->siginfo.blocked;
    sd.pending=&task->siginfo.pending;
    sd.sig=info->si_signo;
    sched_change_task_state_deferred_mask(task,TASK_STATE_RUNNABLE,
                                          __deferred_sig_check,&sd,
                                          state);
  }
}

status_t send_task_siginfo(task_t *task,siginfo_t *info,bool force_delivery)
{
  status_t r;

  LOCK_TASK_SIGNALS(task);
  r=__send_task_siginfo(task,info,force_delivery);
  UNLOCK_TASK_SIGNALS(task);

  if( !r ) {
    __send_siginfo_postlogic(task,info);
  } else if( r == 1 ) {
    kprintf( "send_task_siginfo(): Ignoring signal %d for %d=%d\n",
             info->si_signo,task->pid,task->tid);
  }
  return r < 0 ? r : 0;
}

static status_t __send_signal_to_process(pid_t pid,siginfo_t *siginfo)
{
  task_t *root=pid_to_task(pid);
  task_t *target=NULL;
  int sig=siginfo->si_signo;
  int i,ab=SIGRTMAX;
  list_node_t *ln;

  if( !root ) {
    return -ESRCH;
  }

  if( process_wide_signal(sig) ) {
  } else {
    /* Locate a valid target thread within target process starting from
     * the main thread.
     */
    LOCK_TASK_SIGNALS(root);
    if( can_send_signal_to_task(sig,root) ) {
      target=root;
      if( can_deliver_signal_to_task(sig,root) ) {
        goto send_signal;
      }
      ab=count_active_bits(root->siginfo.pending);
    }
    UNLOCK_TASK_SIGNALS(root);

    /* Signal can't be delivered to the main thread, so try to find a valid
     * target among other threads of target process.
     */
    LOCK_TASK_CHILDS(root);
    list_for_each(&root->threads,ln) {
      task_t *t=container_of(ln,task_t,child_list);

      LOCK_TASK_SIGNALS(t);
      if( can_send_signal_to_task(sig,t) ) {
        if( can_deliver_signal_to_task(sig,t) ) {
          target=t;
          UNLOCK_TASK_CHILDS(root);
          goto send_signal;
        }
        i=count_active_bits(t->siginfo.pending);
        if( i < ab ) {
          target=t;
          ab=i;
        }
      }
      UNLOCK_TASK_SIGNALS(t);
    }
    UNLOCK_TASK_CHILDS(root);
  }

  if( !target ) {
    /* No valid targets found. */
    return 0;
  } else {
    /* Only one valid target was found, so check it one more time. */
    LOCK_TASK_SIGNALS(target);
    if( !can_send_signal_to_task(sig,target) ) {
      UNLOCK_TASK_SIGNALS(target);
      return 0;
    }
    /* Fallthrough. */
  }

send_signal:
  __send_task_siginfo(target,siginfo,false);
  UNLOCK_TASK_SIGNALS(target);
  __send_siginfo_postlogic(target,siginfo);
  return 0;
}

status_t sys_kill(pid_t pid,int sig,siginfo_t *sinfo)
{
  status_t r;
  siginfo_t k_siginfo;

  if( !valid_signal(sig) ) {
    kprintf("sys_kill: bad signal %d!\n",sig);
    return -EINVAL;
  }

  if( is_tid(pid) ) {
    return -ESRCH;
  }

  if( sinfo ) {
    if( !trusted_task(current_task()) ) {
      return -EPERM;
    }

    if( copy_from_user(&k_siginfo,sinfo,sizeof(k_siginfo)) ) {
      return -EFAULT;
    }
  } else {
    memset(&k_siginfo,0,sizeof(k_siginfo));
  }

  k_siginfo.si_signo=sig;
  k_siginfo.si_errno=0;
  k_siginfo.si_pid=current_task()->pid;
  k_siginfo.si_uid=current_task()->uid;
  k_siginfo.si_code=SI_USER;

  if( !pid ) {
    /* Send signal to every process in process group we belong to. */
  } else if( pid > 0 ) {
    /* Send signal to target process. */
    r=__send_signal_to_process(pid,&k_siginfo);
  } else if( pid == -1 ) {
    /* Send signal to all processes except the init process. */
  } else {
    /* PID is lesser than -1: send signal to every process in group -PID */
  }

  return r;
}

status_t sys_sigprocmask(int how,const sigset_t *set,sigset_t *oldset)
{
  task_t *target=current_task();
  sigset_t kset,wset;

  if( how < 0 || how > SIG_SETMASK ) {
    return -EINVAL;
  }

  if( oldset != NULL ) {
    LOCK_TASK_SIGNALS(target);
    kset=target->siginfo.blocked;
    UNLOCK_TASK_SIGNALS(target);

    if( copy_to_user(oldset,&kset,sizeof(kset)) ) {
      return -EFAULT;
    }
  }

  if( set != NULL ) {
    if( copy_from_user(&kset,(void *)set,sizeof(kset)) ) {
      return -EFAULT;
    }
    kset &= ~UNTOUCHABLE_SIGNALS;

    LOCK_TASK_SIGNALS(target);
    wset=target->siginfo.blocked;

    switch( how ) {
      case SIG_BLOCK:
        wset |= kset;
        break;
      case SIG_UNBLOCK:
        wset &= ~kset;
        break;
      case SIG_SETMASK:
        wset=kset;
        break;
    }

    target->siginfo.blocked=wset;
    __update_pending_signals(target);
    UNLOCK_TASK_SIGNALS(target);
  }

  return 0;
}

status_t sys_thread_kill(pid_t process,tid_t tid,int sig)
{
  task_t *target;
  status_t r;
  siginfo_t k_siginfo;

  if( !valid_signal(sig) || !is_tid(tid) ) {
    return -EINVAL;
  }

  target=pid_to_task(tid);
  if( !target ) {
    return -ESRCH;
  }

  if( target->pid != process ) {
    r=-ESRCH;
    goto out;
  }

  memset(&k_siginfo,0,sizeof(k_siginfo));
  k_siginfo.si_signo=sig;
  k_siginfo.si_errno=0;
  k_siginfo.si_pid=current_task()->pid;
  k_siginfo.si_uid=current_task()->uid;
  k_siginfo.si_code=SI_USER;

  r=send_task_siginfo(target,&k_siginfo,false);
out:
  release_task_struct(target);
  return r;
}

static int sigaction(kern_sigaction_t *sact,kern_sigaction_t *oact,
                          int sig) {
  task_t *caller=current_task();
  sa_sigaction_t s=sact->a.sa_sigaction;
  sq_header_t *removed_signals=NULL;

  if( !valid_signal(sig) ) {
    return -EINVAL;
  }

  /* Remove signals that can't be blocked. */
  sact->sa_mask &= ~UNTOUCHABLE_SIGNALS;

  LOCK_TASK_SIGNALS(caller);
  if( oact ) {
    *oact=caller->siginfo.handlers->actions[sig];
  }
  caller->siginfo.handlers->actions[sig]=*sact;

  /* POSIX 3.3.1.3 */
  if( s == SIG_IGN || (s == SIG_DFL && def_ignorable(sig)) ) {
    sigaddset(&caller->siginfo.ignored,sig);

    __update_pending_signals(caller);
    removed_signals=sigqueue_remove_item(&caller->siginfo.sigqueue,sig,true);
  } else {
    sigdelset(&caller->siginfo.ignored,sig);
  }
  UNLOCK_TASK_SIGNALS(caller);

  /* Now we can sefely remove queue items. */
  if( removed_signals != NULL ) {
    list_node_t *last=removed_signals->l.prev;
    list_node_t *next=&removed_signals->l;

    do {
      sq_header_t *h=container_of(next,sq_header_t,l);
      next=next->next;
      free_sigqueue_item(h);
    } while(last != next);
  }
  return 0;
}

long sys_signal(int sig,sa_handler_t handler)
{
  kern_sigaction_t act,oact;
  int r;

  if( (sig == SIGKILL || sig == SIGSTOP) && (sa_sigaction_t)handler != SIG_DFL ) {
    return -EINVAL;
  }

  act.a.sa_handler=handler;
  act.sa_flags=SA_RESETHAND | SA_NODEFER;
  sigemptyset(act.sa_mask);

  r=sigaction(&act,&oact,sig);
  kprintf( "sys_signal(%d,%p): %d\n",
           sig,handler,r);
  return !r ? (long)oact.a.sa_sigaction : r;
}

status_t sys_sigaction(int signum,sigaction_t *act,
                       sigaction_t *oldact)
{
  kern_sigaction_t kact,koact;
  sigaction_t uact;
  status_t r;

  if( !valid_signal(signum) || signum == SIGKILL ||
      signum == SIGSTOP ) {
    return -EINVAL;
  }

  if( !act ) {
    return -EFAULT;
  }

  if( copy_from_user(&uact,act,sizeof(uact)) ) {
    return -EFAULT;
  }

  /* Transform userspace data to kernel data. */
  if( uact.sa_flags & SA_SIGINFO ) {
    kact.a.sa_sigaction=uact.sa_sigaction;
  } else {
    kact.a.sa_handler=uact.sa_handler;
  }
  if( !kact.a.sa_handler ) {
    return -EINVAL;
  }

  kact.sa_mask=uact.sa_mask;
  kact.sa_flags=uact.sa_flags;

  r=sigaction(&kact,oldact ? &koact : NULL,signum);

  if( !r && oldact ) {
    if( copy_to_user(oldact,&koact,sizeof(koact)) ) {
      r=-EFAULT;
    }
  }
  return r;
}

sighandlers_t *allocate_signal_handlers(void)
{
  sighandlers_t *sh=alloc_pages_addr(1,AF_PGEN);

  if( sh ) {
    int i;

    memset(sh,0,sizeof(*sh));
    spinlock_initialize(&sh->lock);
    atomic_set(&sh->use_count,1);

    /* Now setup default signal actions. */
    for(i=0;i<NR_SIGNALS;i++) {
      sh->actions[i].a.sa_sigaction=(_BM(i) & DEFAULT_IGNORED_SIGNALS) ? SIG_IGN : SIG_DFL;
    }
  }
  return sh;
}

sigq_item_t *extract_one_signal_from_queue(task_t *task)
{
  sq_header_t *sh;

  LOCK_TASK_SIGNALS(task);
  sh=sigqueue_remove_first_item(&task->siginfo.sigqueue,false);
  UNLOCK_TASK_SIGNALS(task);

  if( sh != NULL ) {
    return container_of(sh,sigq_item_t,h);
  }
  return NULL;
}
