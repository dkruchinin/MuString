#include <eza/arch/types.h>
#include <eza/task.h>
#include <eza/process.h>
#include <eza/errno.h>
#include <eza/arch/context.h>
#include <kernel/vm.h>
#include <eza/signal.h>
#include <mm/slab.h>
#include <mm/pfalloc.h>

/* Default actions for POSIX signals. */
static sa_sigaction_t __def_actions[NUM_POSIX_SIGNALS]= {
  __SA_TERMINATE,      /* Not supported signal */
  __SA_TERMINATE,      /* SIGHUP */
  __SA_TERMINATE,      /* SIGINT */
};

static status_t __send_siginfo_to_single_task(task_t *task,
                                              siginfo_t *info)
{
  return 0;
}

static status_t __send_siginfo(siginfo_t *info,pid_t pid,bool force_delivery)
{
  task_t *task;
  task_t *caller=current_task();
  signal_struct_t *siginfo;
  int sig=info->si_signo;
  status_t r;

  if( !pid ) {
  } else if ( pid > 0 ) {
    if( is_tid(pid) ) {
      /* Make sure target thread belongs to our process. */
      if( caller->pid != TID_TO_PIDBASE(pid) ) {
        return -EINVAL;
      }
      /* Send signal to a separate thread. */
      task=pid_to_task(pid);
      if( !task ) {
        return -ESRCH;
      }
      siginfo=get_task_siginfo(task);
      if( !siginfo ) {
        goto put_task;
      }
      
    } else {
      /* Send signal to a whole process. */
    }
  } else {
  }

out:
  put_siginfo(siginfo);
  release_task_struct(task);
  return 0;
put_task:
  release_task_struct(task);
  return -ESRCH;
}

status_t sys_kill(pid_t pid,int sig,siginfo_t *sinfo)
{
  pid_t receiver;
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

//  r=__send_siginfo(&k_siginfo,receiver,false);
  r=0;
  kprintf( "sending signal to %d : %d\n",
           pid,r);
  return r;
}

signal_struct_t *allocate_siginfo(void)
{
  signal_struct_t *s=memalloc(sizeof(*s));

  if( s ) {
    memset(s,0,sizeof(*s));
    atomic_set(&s->use_count,1);
  }
  return s;
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
