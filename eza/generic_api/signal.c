#include <eza/arch/types.h>
#include <eza/task.h>
#include <eza/process.h>
#include <eza/errno.h>
#include <eza/arch/context.h>
#include <kernel/vm.h>
#include <eza/signal.h>
#include <mm/slab.h>
#include <mm/pfalloc.h>
#include <eza/security.h>

static memcache_t *sigq_cache;

#define __alloc_sigqueue_item()  alloc_from_memcache(sigq_cache)

struct __def_sig_data {
  sigset_t *blocked;
  int sig;
};

static bool __deferred_sig_check(void *d)
{
  struct __def_sig_data *sd=(struct __def_sig_data *)d;
  return !signal_matches(sd->blocked,sd->sig);
}

void initialize_signals(void)
{
  sigq_cache = create_memcache( "Sigqueue item memcache", sizeof(sigq_item_t),
                                2, SMCF_PGEN);
  if( !sigq_cache ) {
    panic( "initialize_signals(): Can't create the sigqueue item memcache !" );
  }
}

bool update_pending_signals(task_t *task)
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

static void __remove_signal_from_queue(signal_struct_t *siginfo,int sig,
                                       list_head_t *removed_signals)
{
}

/* NOTE: Caller must hold the signal lock !
 * Return codes:
 *   0: signal was successfully queued.
 *   1: signal wasn't queued since it is ignored.
 * -ENOMEM: no memory for a new queue item.
 */
static status_t __send_task_siginfo(task_t *task,siginfo_t *info)
{
  int sig=info->si_signo;
  status_t r;

  /* Make sure only one instance of a non-RT signal is present. */
  if( !rt_signal(sig) && signal_matches(&task->siginfo.pending,sig) ) {
    return 0;
  }

  if( !signal_matches(&task->siginfo.ignored,sig) ) {
    sigq_item_t *qitem=__alloc_sigqueue_item();

    if( qitem ) {
      list_init_node(&qitem->l);
      qitem->info=*info;

      list_add2tail(&task->siginfo.sigqueue,&qitem->l);
      atomic_inc(&task->siginfo.num_pending);

      sigaddset(&task->siginfo.pending,sig);
      r=0;
    } else {
      r=-ENOMEM;
    }
  } else {
    r=1;
  }
  return r;
}

/* NOTE: Called must hold the signal lock ! */
static void __send_siginfo_postlogic(task_t *task,siginfo_t *info)
{
  if( update_pending_signals(task) && task != current_task() ) {
    /* Need to wake up the receiver. */
    struct __def_sig_data sd;
    ulong_t state=TASK_STATE_SLEEPING | TASK_STATE_STOPPED;

    sd.blocked=&task->siginfo.blocked;
    sd.sig=info->si_signo;
    sched_change_task_state_deferred_mask(task,TASK_STATE_RUNNABLE,
                                          __deferred_sig_check,&sd,
                                          state);
  }
}

status_t send_task_siginfo_forced(task_t *task,siginfo_t *info)
{
  return 0;
}

status_t send_task_siginfo(task_t *task,siginfo_t *info)
{
  status_t r;

  LOCK_TASK_SIGNALS(task);
  r=__send_task_siginfo(task,info);
  UNLOCK_TASK_SIGNALS(task);

  if( !r ) {
    __send_siginfo_postlogic(task,info);
  } else if( r == 1 ) {
    kprintf( "send_task_siginfo(): Ignoring signal %d for %d=%d\n",
             info->si_signo,task->pid,task->tid);
  }
  return r < 0 ? r : 0;
}

status_t static __send_pid_siginfo(siginfo_t *info,pid_t pid)
{
  task_t *caller=current_task();
  task_t *task;
  int sig=info->si_signo;
  status_t r;

  if( !pid ) {
    return -2;
  } else if ( pid > 0 ) {
    if( is_tid(pid) ) { /* Send signal to a separate thread. */
      /* Make sure target thread belongs to our process. */
      if( caller->pid != TID_TO_PIDBASE(pid) ) {
        return -EINVAL;
      }
      task=pid_to_task(pid);
      if( !task ) {
        return -ESRCH;
      }
      if( !process_wide_signal(sig) ) {
        /* OK, send signal to target thread. */
        r=send_task_siginfo(task,info);
        release_task_struct(task);
        return r;
      } else {
        /* Trying to send a process-wide signal, so fall-through. */
        release_task_struct(task);
      }
    }
    /* Send signal to a whole process. */
    return -5;
  } else {
    return -10;
  }
}

status_t sys_kill(pid_t pid,int sig,siginfo_t *sinfo)
{
  status_t r;
  siginfo_t k_siginfo;

  if( !valid_signal(sig) ) {
    kprintf("sys_kill: bad signal %d!\n",sig);
    return -EINVAL;
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

  r=__send_pid_siginfo(&k_siginfo,pid);
  kprintf( ">> sending signal to %d : %d\n",
           pid,r);
  return r;
}

static status_t sigaction(kern_sigaction_t *sact,kern_sigaction_t *oact,
                          int sig) {
  task_t *caller=current_task();
  kern_sigaction_t *sa=&caller->siginfo.handlers->actions[sig];
  sa_sigaction_t s=sact->a.sa_sigaction;
  list_head_t removed_signals;

  if( !valid_signal(sig) ) {
    return -EINVAL;
  }

  list_init_head(&removed_signals);

  /* Remove signals that can't be blocked. */
  sa->sa_mask &= ~UNTOUCHABLE_SIGNALS;

  LOCK_TASK_SIGNALS(caller);
  *oact=*sa;
  *sa=*sact;

  /* POSIX 3.3.1.3 */
  if( s == SIG_IGN || (s == SIG_DFL && def_ignorable(sig)) ) {
    sigaddset(&caller->siginfo.ignored,sig);
    sigdelset(&caller->siginfo.pending,sig);

    __remove_signal_from_queue(&caller->siginfo,sig,&removed_signals);
    update_pending_signals(caller);
  } else {
    sigdelset(&caller->siginfo.ignored,sig);
  }
  UNLOCK_TASK_SIGNALS(caller);

  /* Now we can sefely remove queue items. */
  if( !list_is_empty(&removed_signals) ) {
  }
  return 0;
}

sa_sigaction_t sys_signal(int sig,sa_handler_t handler)
{
  kern_sigaction_t act,oact;
  status_t r;

  act.a.sa_handler=handler;
  act.sa_flags=SA_RESETHAND | SA_NODEFER;
  sigemptyset(act.sa_mask);

  r=sigaction(&act,&oact,sig);
  return !r ? oact.a.sa_sigaction : SIG_ERR;
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
    for(i=0;i<NUM_POSIX_SIGNALS;i++) {
      sh->actions[i].a.sa_sigaction=(_BM(i) & DEFAULT_IGNORED_SIGNALS) ? SIG_IGN : SIG_DFL;
    }
  }
  return sh;
}

sigq_item_t *extract_one_signal_from_queue(task_t *task)
{
  list_node_t *n;

  LOCK_TASK_SIGNALS(task);
  if( !list_is_empty(&task->siginfo.sigqueue) ) {
    n=list_node_first(&task->siginfo.sigqueue);
    list_del(n);
    atomic_dec(&task->siginfo.num_pending);
  } else {
    n=NULL;
  }
  UNLOCK_TASK_SIGNALS(task);

  if( n ) {
    return container_of(n,sigq_item_t,l);
  }
  return NULL;
}
