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
 * include/mstring/signal.h: generic data types and prototypes for kernel signal
 *                       delivery subsystem.
 */

#ifndef __SIGNAL_H__
#define  __SIGNAL_H__

#include <mstring/types.h>
#include <mstring/time.h>
#include <mstring/task.h>
#include <sync/spinlock.h>
#include <arch/atomic.h>
#include <arch/signal.h>
#include <mm/slab.h>
#include <mstring/bits.h>
#include <ds/list.h>
#include <mstring/sigqueue.h>
#include <mstring/siginfo.h>
#include <mstring/gc.h>
#include <security/security.h>
#include <security/util.h>

#define NUM_POSIX_SIGNALS  32
#define NUM_RT_SIGNALS     32

#define	_SIG_MAXSIG	63
#define NR_SIGNALS  (NUM_POSIX_SIGNALS + NUM_RT_SIGNALS)

#define valid_signal(n)  ((n)>0 && (n) <= SIGRTMAX )
#define rt_signal(n)  ((n)>=SIGRTMIN && (n) <= SIGRTMAX)


typedef void (*sa_handler_t)(int);
typedef void (*sa_sigaction_t)(int,usiginfo_t *,void *);

typedef struct __sigaction {
  union {
    sa_handler_t sa_handler;
    sa_sigaction_t sa_sigaction;
  };
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

#define SIGEV_SIGNAL         0
#define SIGEV_SIGNAL_THREAD  1

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

/*
 * System defined signals.
 */
#define	SIGHUP		1	/* hangup */
#define	SIGINT		2	/* interrupt */
#define	SIGQUIT		3	/* quit */
#define	SIGILL		4	/* illegal instr. (not reset when caught) */
#define	SIGTRAP		5	/* trace trap (not reset when caught) */
#define	SIGABRT		6	/* abort() */
#define	SIGIOT		SIGABRT	/* compatibility */
#define	SIGEMT		7	/* EMT instruction */
#define	SIGFPE		8	/* floating point exception */
#define	SIGKILL		9	/* kill (cannot be caught or ignored) */
#define	SIGBUS		10	/* bus error */
#define	SIGSEGV		11	/* segmentation violation */
#define	SIGSYS		12	/* non-existent system call invoked */
#define	SIGPIPE		13	/* write on a pipe with no one to read it */
#define	SIGALRM		14	/* alarm clock */
#define	SIGTERM		15	/* software termination signal from kill */
#define	SIGURG		16	/* urgent condition on IO channel */
#define	SIGSTOP		17	/* sendable stop signal not from tty */
#define	SIGTSTP		18	/* stop signal from tty */
#define	SIGCONT		19	/* continue a stopped process */
#define	SIGCHLD		20	/* to parent on child stop or exit */
#define	SIGTTIN		21	/* to readers pgrp upon background tty read */
#define	SIGTTOU		22	/* like TTIN if (tp->t_local&LTOSTOP) */
#define	SIGIO		23	/* input/output possible signal */
#define	SIGXCPU		24	/* exceeded CPU time limit */
#define	SIGXFSZ		25	/* exceeded file size limit */
#define	SIGVTALRM	26	/* virtual time alarm */
#define	SIGPROF		27	/* profiling time alarm */
#define	SIGWINCH	28	/* window size changes */
#define	SIGINFO		29	/* information request */
#define	SIGUSR1		30	/* user defined signal 1 */
#define	SIGUSR2		31	/* user defined signal 2 */
#define	SIGTHR		32	/* reserved by thread library. */
#define	SIGLWP		SIGTHR

#define SIGSTKFLT   33
#define SIGPOLL     SIGIO /* Pollable event occurred (System V) */
#define SIGPWR      34    /* Power failure restart (System V) */

#define	SIGRTMIN	35
#define	SIGRTMAX	(_SIG_MAXSIG - 1)

#define process_wide_signal(s)  ((s) & (_BM(SIGTERM) | _BM(SIGSTOP)) )

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
  usiginfo_t info;
  void *kern_priv;  /* Kernel private data for deffered signal processing. */
} sigq_item_t;

void initialize_signals(void);
#define free_sigqueue_item(i)  memfree((i))
sigq_item_t *extract_one_signal_from_queue(task_t *task);

#define pending_signals_present(t) ((t)->siginfo.pending != 0)

/**
 * @fn int send_task_siginfo(task_t *task,usiginfo_t *info,bool force_delivery,
 *                           void *kern_priv,task_t *sender);
 * Send a signal to target task, forcing delivery if necessary.
 *
 * This function sends a signal to target task. If the signal is unblocked
 * in task's set of ignored signals, kernel makes the signal pending for
 * the process if it is not set in process's set of blocked signals too.
 * If all chescks are passed, the signal will be delivered during the nearest
 * return to userspace. Othherwise, the signal will be delayed.
 * Note: only one instance of non-realtime signals may be pending at the same
 * moment.
 *
 * @param task target task.
 * @param info extended signal information to be send along with the signal.
 * @param force_delivery prevent the signal from being ignored by the process's
 *        set of ignored signals, and set its action to default in case it was
 *        explicitely set to default.
 * @param kern_priv kernel private data to be sent along with the signal.
 * @param sender task that initiated this operation. In case the signal is being
 * sent by the kernel, this value should be NULL.
 *
 * @return In case the signal was successfully queued (or discarded), this
 * function returns zero. Otherwise, one of the following error codes returned
 * (negated values).
 *
 *     ENOMEM no memory was available for a new signal structure.
 *
 *     EPERM  sending task can't send signals to the target.
 *
 */
int send_task_siginfo(task_t *task,usiginfo_t *info,bool force_delivery,
                      void *kern_priv,task_t *sender);

int send_process_siginfo(pid_t pid,usiginfo_t *siginfo,void *kern_priv,
                         task_t *sender, bool broadcast);

bool update_pending_signals(task_t *task);
bool __update_pending_signals(task_t *task);
sighandlers_t * allocate_signal_handlers(void);
void free_signal_handlers(sighandlers_t * sh);
void process_sigitem_private(sigq_item_t *sigitem);

void schedule_user_deferred_action(task_t *target,gc_action_t *a,bool force);

#define first_signal_in_set(s) (arch_bit_find_lsf(*(long *)(s)))

static inline bool task_was_interrupted(task_t *t) {
    return read_task_pending_uworks(t) != 0 ||
        deliverable_signals_present(&t->siginfo);
}

static inline bool is_lethal_signal(int sig)
{
  return (_BM(sig) & LETHAL_SIGNALS);
}


#endif
