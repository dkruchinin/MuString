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
 * mstring/generic_api/signal.c: generic code of kernel signal delivery subsystem.
 */

#include <arch/types.h>
#include <mstring/task.h>
#include <mstring/process.h>
#include <mstring/errno.h>
#include <arch/context.h>
#include <mstring/signal.h>
#include <mm/slab.h>
#include <mm/page_alloc.h>
#include <mstring/usercopy.h>
#include <mstring/posix.h>
#include <mstring/kconsole.h>

static memcache_t *sigq_cache;

#define __alloc_sigqueue_item()  alloc_from_memcache(sigq_cache, 0)

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
  sigq_cache = create_memcache("Sigqueue items", sizeof(sigq_item_t),
                              1, MMPOOL_KERN | SMCF_IMMORTAL | SMCF_LAZY);
  if( !sigq_cache ) {
    panic( "initialize_signals(): Can't create the sigqueue item memcache !" );
  }
}

/* NOTE: Signals must be locked before calling this function ! */
bool __update_pending_signals(task_t *task)
{
  signal_struct_t *siginfo=&task->siginfo;
  bool delivery_needed;

  if( deliverable_signals_present(siginfo) ||
      !list_is_empty(&task->uworks_data.def_uactions) ) {
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
  int is;

  LOCK_TASK_SIGNALS_INT(task,is);
  r=__update_pending_signals(task);
  UNLOCK_TASK_SIGNALS_INT(task,is);

  return r;
}

/* NOTE: Caller must hold the signal lock !
 * Return codes:
 *   0: signal was successfully queued.
 *   1: signal wasn't queued since it was ignored.
 * -ENOMEM: no memory for a new queue item.
 */
static int __send_task_siginfo(task_t *task,usiginfo_t *info,
                               void *kern_priv,bool force_delivery)
{
  int sig=info->si_signo;
  int r;
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
  if( !rt_signal(sig) &&
      (signal_matches(&task->siginfo.pending,sig) && !kern_priv)) {
    return 0;
  }

  if( send_signal ) {
    sigq_item_t *qitem=__alloc_sigqueue_item();

    if( qitem ) {
      qitem->h.idx=sig;
      qitem->info=*info;
      qitem->kern_priv=kern_priv;

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

static void __send_siginfo_postlogic(task_t *task,usiginfo_t *info)
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

int send_task_siginfo(task_t *task,usiginfo_t *info,bool force_delivery,
                      void *kern_priv)
{
  int r,is;

  LOCK_TASK_SIGNALS_INT(task,is);
  r=__send_task_siginfo(task,info,kern_priv,force_delivery);
  UNLOCK_TASK_SIGNALS_INT(task,is);

  if( !r ) {
    __send_siginfo_postlogic(task,info);
  } else if( r == 1 ) {
    kprintf( "send_task_siginfo(): Ignoring signal %d for %d=%d\n",
             info->si_signo,task->pid,task->tid);
  }
  return r < 0 ? r : 0;
}

int send_process_siginfo(pid_t pid,usiginfo_t *siginfo,void *kern_priv)
{
  task_t *root=pid_to_task(pid);
  task_t *target=NULL;
  int sig=siginfo->si_signo;
  int i,is1,ab=SIGRTMAX;
  list_node_t *ln;
  bool unlock_childs=false;

  is1 = 0; /* to shut up gcc warning */
  if( !root ) {
    return -ESRCH;
  }

  if( process_wide_signal(sig) ) {
  } else {
    /* Locate a valid target thread within target process starting from
     * the main thread.
     */
    interrupts_save_and_disable(is1);
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
          unlock_childs=true;
          target=t;
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
    interrupts_restore(is1);
    return 0;
  } else {
    /* Only one valid target was found, so check it one more time. */
    LOCK_TASK_SIGNALS(target);
    if( !can_send_signal_to_task(sig,target) ) {
      UNLOCK_TASK_SIGNALS(target);
      interrupts_restore(is1);
      return 0;
    }
    /* Fallthrough. */
  }

send_signal:
  __send_task_siginfo(target,siginfo,kern_priv,false);
  UNLOCK_TASK_SIGNALS(target);

  if( unlock_childs ) {
    UNLOCK_TASK_CHILDS(root);
  }
  interrupts_restore(is1);

  __send_siginfo_postlogic(target,siginfo);
  return 0;
}

int sys_kill(pid_t pid,int sig,usiginfo_t *sinfo)
{
  int r = 0;
  usiginfo_t k_siginfo;

  if( !valid_signal(sig) ) {
    kprintf_dbg("sys_kill: bad signal %d!\n",sig);
    return -EINVAL;
  }

  if( sinfo ) {
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
    r=send_process_siginfo(pid,&k_siginfo,NULL);
  } else if( pid == -1 ) {
    /* Send signal to all processes except the init process. */
  } else {
    /* PID is lesser than -1: send signal to every process in group -PID */
  }

  return r;
}

long sys_sigprocmask(int how,sigset_t *set,sigset_t *oldset)
{
  task_t *target=current_task();
  sigset_t kset,wset;
  int is;

  if( how < 0 || how > SIG_SETMASK ) {
    return -EINVAL;
  }

  if( oldset != NULL ) {
    LOCK_TASK_SIGNALS_INT(target,is);
    kset=target->siginfo.blocked;
    UNLOCK_TASK_SIGNALS_INT(target,is);

    if( copy_to_user(oldset,&kset,sizeof(kset)) ) {
      return -EFAULT;
    }
  }

  if( set != NULL ) {
    if( copy_from_user(&kset,(void *)set,sizeof(kset)) ) {
      return -EFAULT;
    }
    kset &= ~UNTOUCHABLE_SIGNALS;

    LOCK_TASK_SIGNALS_INT(target,is);
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
    UNLOCK_TASK_SIGNALS_INT(target,is);
  }

  return 0;
}

long sys_thread_kill(pid_t process,tid_t tid,int sig)
{
  task_t *target;
  long r;
  usiginfo_t k_siginfo;

  if( !valid_signal(sig) ) {
    return -EINVAL;
  }

  target=lookup_task(process,tid,0);
  if( !target ) {
    return -ESRCH;
  }

  memset(&k_siginfo,0,sizeof(k_siginfo));
  k_siginfo.si_signo=sig;
  k_siginfo.si_errno=0;
  k_siginfo.si_pid=current_task()->pid;
  k_siginfo.si_uid=current_task()->uid;
  k_siginfo.si_code=SI_USER;

  r=send_task_siginfo(target,&k_siginfo,false,NULL);
  release_task_struct(target);
  return r;
}

static int sigaction(kern_sigaction_t *sact,kern_sigaction_t *oact,
                     int sig) {
  task_t *caller=current_task();
  sa_sigaction_t s=sact->a.sa_sigaction;
  sq_header_t *removed_signals=NULL;
  int is;

  if( !valid_signal(sig) ) {
    return -EINVAL;
  }

  /* Remove signals that can't be blocked. */
  sact->sa_mask &= ~UNTOUCHABLE_SIGNALS;

  LOCK_TASK_SIGNALS_INT(caller,is);
  if( oact ) {
    *oact=caller->siginfo.handlers->actions[sig];
  }
  caller->siginfo.handlers->actions[sig]=*sact;

  /* POSIX 3.3.1.3 */
  if( s == SIG_IGN || (s == SIG_DFL && def_ignorable(sig)) ) {
    sigaddset(&caller->siginfo.ignored,sig);

    removed_signals=sigqueue_remove_item(&caller->siginfo.sigqueue,sig,true);
    __update_pending_signals(caller);
  } else {
    sigdelset(&caller->siginfo.ignored,sig);
  }
  UNLOCK_TASK_SIGNALS_INT(caller,is);

  /* Now we can sefely remove all dequeued items. */
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
  return !r ? (long)oact.a.sa_sigaction : r;
}

int sys_sigaction(int signum,sigaction_t *act,
                       sigaction_t *oldact)
{
  kern_sigaction_t kact,koact;
  sigaction_t uact;
  int r;

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
  sighandlers_t *sh=alloc_pages_addr(1, MMPOOL_KERN);

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
  int is;

  LOCK_TASK_SIGNALS_INT(task,is);
  sh=sigqueue_remove_first_item(&task->siginfo.sigqueue,false);
  UNLOCK_TASK_SIGNALS_INT(task,is);

  if( sh != NULL ) {
    return container_of(sh,sigq_item_t,h);
  }
  return NULL;
}

void schedule_user_deferred_action(task_t *target,gc_action_t *a,bool force)
{
  int is;

  LOCK_TASK_SIGNALS_INT(target,is);
  list_add2tail(&target->uworks_data.def_uactions,&a->l);
  __update_pending_signals(target);
  UNLOCK_TASK_SIGNALS_INT(target,is);
}

void process_sigitem_private(sigq_item_t *sigitem)
{
  if( !sigitem->kern_priv ) {
    return;
  }

  /* Have to perform some signal-related kernel work (for example,
   * rearm the timer related to this signal).
   */
  posix_timer_t *ptimer=(posix_timer_t *)sigitem->kern_priv;

#ifdef CONFIG_DEBUG_TIMERS
  kprintf_fault("process_sigitem_private() [%d:%d]: Tick=%d, Processing timer %p\n",
                current_task()->pid,current_task()->tid,system_ticks,
                &ptimer->ktimer);
#endif

  LOCK_POSIX_STUFF_W(stuff);
  if( ptimer->interval ) {
    ulong_t next_tick=ptimer->ktimer.time_x+ptimer->interval;
    ulong_t overrun;

    /* Calculate overrun for this timer,if any. */
    if( next_tick <= system_ticks ) {
      overrun=(system_ticks-ptimer->ktimer.time_x)/ptimer->interval;
      next_tick=system_ticks+ptimer->interval;
    } else {
      overrun=0;
    }

#ifdef CONFIG_DEBUG_TIMERS
    kprintf_fault("process_sigitem_private() [%d:%d]: Tick=%d, timer %p, next TX=%d\n",
                  current_task()->pid,current_task()->tid,system_ticks,
                  &ptimer->ktimer,next_tick);
#endif

    /* Rearm this timer. We take only active timers into account. */
    ptimer->overrun=overrun;

    if( posix_timer_active(ptimer) ) {
      modify_timer(&ptimer->ktimer,next_tick);
    }
  }
  UNLOCK_POSIX_STUFF_W(stuff);
}

long sys_sigwaitinfo(sigset_t *set,int *sig,usiginfo_t *info,
                     timespec_t *timeout)
{
  sigset_t kset;
  int is,sidx,r=0;
  task_t *caller=current_task();
  signal_struct_t *sigstruct=&caller->siginfo;
  sigset_t *pending=&sigstruct->pending;
  sq_header_t *sh;
  int dneeded=-1;
  timespec_t ktv,*ptv;

  if( !sig && !info ) {
    return -EFAULT;
  }

  if( copy_from_user(&kset,set,sizeof(kset)) ) {
    return -EFAULT;
  }

  if( !kset || (kset & UNTOUCHABLE_SIGNALS) ) {
    return -EINVAL;
  }

  if( timeout ) {
    if( copy_from_user(&ktv,timeout,sizeof(ktv)) ) {
      return -EFAULT;
    }
    if( !timeval_is_valid(&ktv) ) {
      return -EINVAL;
    }
    ptv=&ktv;
  } else {
    ptv=NULL;
  }

  /* First, unblock target signals and chek that caller has blocked them. */
  LOCK_TASK_SIGNALS_INT(caller,is);

#ifdef CONFIG_DEBUG_SIGNALS
  kprintf_fault("sys_sigwaitinfo() [%d:%d] <START> Kset=%p, Blocked=%p, Tick=%d, CHECK=%p\n",
                current_task()->pid,current_task()->tid,kset,sigstruct->blocked,
                system_ticks,kset & sigstruct->blocked);
#endif

  if( (kset & sigstruct->blocked) != kset ) {
    r=-EINVAL;
    goto unlock_signals;
  } else {
    sigstruct->blocked &= ~kset;
  }

  UNLOCK_TASK_SIGNALS_INT(caller,is);

  while( true ) {
    LOCK_TASK_SIGNALS_INT(caller,is);

#ifdef CONFIG_DEBUG_SIGNALS
  kprintf_fault("sys_sigwaitinfo() [%d:%d] Tick=%d *pending=%p, Kset=%p\n",
                current_task()->pid,current_task()->tid,
                system_ticks,*pending,kset);
#endif

    if( *pending & kset ) {
      sidx=first_signal_in_set(pending);
      sh=sigqueue_remove_item(&caller->siginfo.sigqueue,sidx,false);
      ASSERT(sh);

#ifdef CONFIG_DEBUG_SIGNALS
  kprintf_fault("sys_sigwaitinfo() [%d:%d] Tick=%d Signal matches ! Kset=%p, SIG=%d\n",
                current_task()->pid,current_task()->tid,
                system_ticks,kset,sidx);
#endif

    } else {
      sh=NULL;
    }

    if( sh ) {
      sigq_item_t *sigitem=(sigq_item_t *)sh;

      UNLOCK_TASK_SIGNALS_INT(caller,is);
      /* Signal might have some associated private actions (like timer
       * rearming, etc). So take it into account.
       */
      if( sigitem->kern_priv ) {
        process_sigitem_private(sigitem);
      }

      if( sig ) {
        r=copy_to_user(sig,&sidx,sizeof(*sig)) ? -EFAULT : 0;
      }

      if( !r && info ) {
        r=copy_to_user(info,&sigitem->info,sizeof(*info)) ? -EFAULT : 0;
      }
      free_sigqueue_item(sigitem);
      break;
    }

    if( deliverable_signals_present(sigstruct) ) {
      /* Bad luck - we were interrupted by a different signal. */
#ifdef CONFIG_DEBUG_SIGNALS
  kprintf_fault("sys_sigwaitinfo() [%d:%d] Bad luck ! Got a different signal (pending=%p,blocked=%p), Tick=%d\n",
                current_task()->pid,current_task()->tid,sigstruct->pending,
                sigstruct->blocked,system_ticks);
#endif
      r=-EINTR; /* Fallthrough. */
    }
    UNLOCK_TASK_SIGNALS_INT(caller,is);

    if( r ) {
      break;
    } else {
      if( ptv ) {
        r=-EAGAIN;
        if( !(ktv.tv_sec | ktv.tv_nsec) ) {
          break;
        }
        if( !sleep(time_to_ticks(ptv)) ) {
          break;
        }
      } else {
        put_task_into_sleep(caller);
      }
    }
  }

  /* Now block target signals again and recalculate pending signals. */
  LOCK_TASK_SIGNALS_INT(caller,is);
  sigstruct->blocked |= kset;
  dneeded=__update_pending_signals(caller);
unlock_signals:

#ifdef CONFIG_DEBUG_SIGNALS
  kprintf_fault("sys_sigwaitinfo() [%d:%d] <END> Tick=%d, r=%d, Kset=%p (Blocked=%p, Pending=%p), DELIVERY=%d\n",
                current_task()->pid,current_task()->tid,
                system_ticks,r,kset,sigstruct->blocked,sigstruct->pending,
                dneeded);
#endif

  UNLOCK_TASK_SIGNALS_INT(caller,is);
  return r;
}
