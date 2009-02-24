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
 * include/eza/signal.h: generic data types and prototypes for kernel signal
 *                       delivery subsystem.
 */

#ifndef __SIGNAL_H__
#define  __SIGNAL_H__

#include <eza/arch/types.h>
#include <eza/time.h>
#include <eza/task.h>
#include <eza/spinlock.h>
#include <eza/arch/atomic.h>
#include <eza/arch/signal.h>
#include <mm/slab.h>
#include <eza/bits.h>
#include <ds/list.h>
#include <eza/sigqueue.h>
#include <eza/gc.h>

#define NUM_POSIX_SIGNALS  32
#define NUM_RT_SIGNALS     32

#define SIGRTMIN  NUM_POSIX_SIGNALS
#define SIGRTMAX  ((SIGRTMIN+NUM_RT_SIGNALS)-1)
#define NR_SIGNALS  (NUM_POSIX_SIGNALS + NUM_RT_SIGNALS)

#define valid_signal(n)  ((n)>0 && (n) <= SIGRTMAX )
#define rt_signal(n)  ((n)>=SIGRTMIN && (n) <= SIGRTMAX)

typedef union sigval {
  int sival_int;
  void *sival_ptr;
} sigval_t;

typedef struct __siginfo {
  int       si_signo;    /* Signal number */
  int       si_errno;    /* An errno value */
  int       si_code;     /* Signal code */
  pid_t     si_pid;      /* Sending process ID */
  uid_t     si_uid;      /* Real user ID of sending process */
  int       si_status;   /* Exit value or signal */
  clock_t   si_utime;    /* User time consumed */
  clock_t   si_stime;    /* System time consumed */
  sigval_t  si_value;    /* Signal value */
  int       si_int;      /* POSIX.1b signal */
  void     *si_ptr;      /* POSIX.1b signal */
  void     *si_addr;     /* Memory location which caused fault */
  int       si_band;     /* Band event */
  int      si_fd;       /* File descriptor */
} siginfo_t;

#define INIT_SIGINFO_CURR(s)  do {              \
  memset((s),0,sizeof(siginfo_t));              \
  (s)->si_pid=current_task()->pid;              \
  (s)->si_uid=current_task()->uid;              \
  } while(0)

#define SI_USER    0 /* Sent by user */
#define SI_KERNEL  1 /* Sent by kernel */

typedef void (*sa_handler_t)(int);
typedef void (*sa_sigaction_t)(int,siginfo_t *,void *);

typedef struct __sigaction {
  sa_handler_t sa_handler;
  sa_sigaction_t sa_sigaction;
  sigset_t   sa_mask;
  int        sa_flags;
  void     (*sa_restorer)(void);
} sigaction_t;

#define SA_NOCLDSTOP  0x1
#define SA_NOCLDWAIT  0x2
#define SA_RESETHAND  0x4
#define SA_ONSTACK    0x8
#define SA_RESTART    0x10
#define SA_NODEFER    0x20
#define SA_SIGINFO    0x40

#define sigemptyset(s) ((s)=0)

typedef struct __kern_sigaction {
  union {
    sa_sigaction_t sa_sigaction;
    sa_handler_t sa_handler;
  } a;
  sigset_t sa_mask;
  int sa_flags;
} kern_sigaction_t;

#define SIGEV_SIGNAL  0

typedef struct __sighandlers {
  atomic_t use_count;
  kern_sigaction_t actions[NR_SIGNALS];
  spinlock_t lock;
} sighandlers_t;

#define SIGNAL_PENDING(set,sig)  arch_bit_test((set),sig)

static inline void put_signal_handlers(sighandlers_t *s)
{
  if( atomic_dec_and_test(&s->use_count ) ) {
    memfree(s);
  }
}

#define SIG_IGN  ((sa_sigaction_t)0)
#define SIG_DFL  ((sa_sigaction_t)1)
#define SIG_ERR  ((sa_sigaction_t)-1)

#define SIG_BLOCK    0
#define SIG_UNBLOCK  1
#define SIG_SETMASK  2  /* Must be the maximum value. */

#define SIGHUP     1
#define SIGINT     2
#define SIGQUIT    3
#define SIGILL     4
#define SIGTRAP    5
#define SIGABRT    6
#define SIGIOT     6
#define SIGBUS     7
#define SIGFPE     8
#define SIGKILL    9
#define SIGUSR1    10
#define SIGSEGV    11
#define SIGUSR2    12
#define SIGPIPE	   13
#define SIGALRM	   14
#define SIGTERM	   15
#define SIGSTKFLT  16
#define SIGCHLD    17
#define SIGCONT    18
#define SIGSTOP    19
#define SIGTSTP    20
#define SIGTTIN	   21
#define SIGTTOU	   22
#define SIGURG	   23
#define SIGXCPU    24
#define SIGXFSZ	   25
#define SIGVTALRM  26
#define SIGPROF	   27
#define SIGWINCH   28
#define SIGIO      29
#define SIGPOLL    SIGIO
#define SIGPWR     30
#define SIGSYS     31

#define process_wide_signal(s)  ((s) & (_BM(SIGTERM) | _BM(SIGSTOP)) )

/* si_code for SIGSEGV signal. */
#define SEGV_MAPERR  0x1  /** address not mapped **/
#define SEGV_ACCERR  0x2  /** invalid permissions **/

#define DEFAULT_IGNORED_SIGNALS (_BM(SIGCHLD) | _BM(SIGURG) | _BM(SIGWINCH))
#define UNTOUCHABLE_SIGNALS (_BM(SIGKILL) | _BM(SIGSTOP))

#define LETHAL_SIGNALS  (_BM(SIGHUP) | _BM(SIGINT) | _BM(SIGKILL) | _BM(SIGPIPE) | _BM(SIGALRM) \
                         | _BM(SIGTERM) | _BM(SIGUSR1) | _BM(SIGUSR2) | _BM(SIGPOLL) | _BM(SIGPROF) \
                         | _BM(SIGVTALRM) | _BM(SIGSEGV) )

#define def_ignorable(s) (_BM(s) & DEFAULT_IGNORED_SIGNALS)

#define deliverable_signals_present(s) ((s)->pending & ~((s)->blocked))
#define can_send_signal_to_task(s,t) (!signal_matches(&(t)->siginfo.ignored,(s)))
#define can_deliver_signal_to_task(s,t) (!signal_matches(&(t)->siginfo.blocked,(s)))

typedef struct __sigq_item {
  sq_header_t h;   /* Must be the first member ! */
  siginfo_t info;
  void *kern_priv;  /* Kernel private data for deffered signal processing. */
} sigq_item_t;

void initialize_signals(void);
#define free_sigqueue_item(i)  memfree((i))
sigq_item_t *extract_one_signal_from_queue(task_t *task);

#define pending_signals_present(t) ((t)->siginfo.pending != 0)

int send_task_siginfo(task_t *task,siginfo_t *info,bool force_delivery);
int send_process_siginfo(pid_t pid,siginfo_t *siginfo,void *kern_priv);
bool update_pending_signals(task_t *task);
bool __update_pending_signals(task_t *task);
int send_task_siginfo_forced(task_t *task,siginfo_t *info);
sighandlers_t * allocate_signal_handlers(void);
void process_sigitem_private(sigq_item_t *sigitem);

void schedule_user_deferred_action(task_t *target,gc_action_t *a,bool force);

#define first_signal_in_set(s) (arch_bit_find_lsf(*(long *)(s)))

static inline bool task_was_interrupted(task_t *t) {
  return read_task_pending_uworks(t) != 0 || deliverable_signals_present(&t->siginfo);
}

#endif
