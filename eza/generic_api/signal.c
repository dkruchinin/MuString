#include <eza/arch/types.h>
#include <eza/task.h>
#include <eza/process.h>
#include <eza/errno.h>
#include <eza/arch/context.h>
#include <kernel/vm.h>
#include <eza/signal.h>
#include <mm/slab.h>
#include <mm/pfalloc.h>

static memcache_t *sigq_cache;

/* Default actions for POSIX signals. */
static sa_sigaction_t __def_actions[NUM_POSIX_SIGNALS]= {
  SIG_IGN,      /* Not supported signal */
  SIG_DFL,      /* SIGHUP */
  SIG_DFL,      /* SIGINT */
};

#define __alloc_sigqueue_item()  alloc_from_memcache(sigq_cache)
#define __free_sigqueue_item(i)  memfree((i))

void initialize_signals(void)
{
  sigq_cache = create_memcache( "Sigqueue item memcache", sizeof(sigq_item_t),
                                2, SMCF_PGEN);
  if( !sigq_cache ) {
    panic( "initialize_signals(): Can't create the sigqueue item memcache !" );
  }
}

status_t send_task_siginfo(task_t *task,siginfo_t *info)
{
  int sig=info->si_signo;
  bool wakeup=false;
  status_t r;

  kprintf( ">>>> [%d] IGNORED: 0x%X\n", sizeof(*info),task->siginfo.ignored );

  LOCK_TASK_SIGNALS(task);
  if( !signal_matches(&task->siginfo.ignored,sig) ) {
    sigq_item_t *qitem=__alloc_sigqueue_item();

    if( qitem ) {
      list_init_node(&qitem->l);
      qitem->info=*info;

      list_add2tail(&task->siginfo.sigqueue,&qitem->l);
      atomic_inc(&task->siginfo.num_pending);

      set_signal(&task->siginfo.pending,sig);
      wakeup=!signal_matches(&task->siginfo.blocked,sig);
      r=0;
    } else {
      r=-ENOMEM;
    }
  }
  UNLOCK_TASK_SIGNALS(task);

  if( wakeup ) {
  }

  return 0;
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
      sh->actions[i].sa_handler=__def_actions[i];
    }
  }
  return sh;
}
