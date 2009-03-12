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
 * include/eza/task.h: generic functions for dealing with task creation.
 */


#ifndef __TASK_H__
#define __TASK_H__ 

#include <mlibc/types.h>
#include <eza/kstack.h>
#include <eza/arch/context.h>
#include <mm/page.h>
#include <mm/vmm.h>
#include <eza/limits.h>
#include <eza/sigqueue.h>
#include <eza/mutex.h>
#include <eza/event.h>
#include <eza/scheduler.h>
#include <ds/idx_allocator.h>
#include <eza/arch/atomic.h>

typedef uint32_t time_slice_t;


#define INVALID_PID  ((pid_t)~0) 
/* TODO: [mt] Manage NUM_PIDS properly ! */
#define NUM_PIDS  32768

#define TASK_PRIO_INVAL ((uint32_t)~0)

/* PID-to-task_t translation hash stuff */
#define PID_HASH_LEVEL_SHIFT  9 /* Levels of PID-to-task cache. */
#define PID_HASH_LEVELS  (1 << PID_HASH_LEVEL_SHIFT)
#define PID_HASH_LEVEL_MASK  (PID_HASH_LEVELS-1)

/* TID-related macros. */
#define TID_SHIFT  16
#define MAX_THREADS_PER_PROCESS  (1<<TID_SHIFT)
#define GENERATE_TID(pid,tid) (((pid)<<TID_SHIFT) | tid)
#define TID_TO_PIDBASE(tid)  ((tid)>>TID_SHIFT)
#define TID(tid) ((tid) & ~(MAX_THREADS_PER_PROCESS-1))

#define is_tid(t)  ((t) >= (1<<TID_SHIFT))

/* Macros for locking task structure. */
#define LOCK_TASK_STRUCT(t) spinlock_lock(&t->lock)
#define UNLOCK_TASK_STRUCT(t) spinlock_unlock(&t->lock)

#define LOCK_TASK_CHILDS(t) spinlock_lock(&(t)->child_lock) 
#define UNLOCK_TASK_CHILDS(t) spinlock_unlock(&(t)->child_lock)

#define LOCK_TASK_MEMBERS(t) spinlock_lock(&t->member_lock)
#define UNLOCK_TASK_MEMBERS(t) spinlock_unlock(&t->member_lock)
#define TASK_EVENT_TERMINATION  0x1
#define NUM_TASK_EVENTS  1
#define ALL_TASK_EVENTS_MASK  ((1<<NUM_TASK_EVENTS)-1)
#define LOCK_TASK_EVENTS_R(t)
#define UNLOCK_TASK_EVENTS_R(t)
#define LOCK_TASK_EVENTS_W(t)
#define UNLOCK_TASK_EVENTS_W(t)

typedef struct __task_event_ctl_arg {
  ulong_t ev_mask;
  ulong_t port;
} task_event_ctl_arg;

typedef struct __task_event_descr {
  pid_t pid;
  tid_t tid;
  ulong_t ev_mask;
} task_event_descr_t;

struct __ipc_gen_port;

typedef struct __task_event_listener {
  struct __ipc_gen_port *port;
  struct __task_struct *listener;
  list_node_t owner_list;
  list_node_t llist;
  ulong_t events;
} task_event_listener_t;

typedef struct __task_events {
  list_head_t my_events;
  list_head_t listeners;
} task_events_t;

typedef enum __task_creation_flag_t {
  CLONE_MM       = 0x01,
  CLONE_IPC      = 0x02,
  CLONE_SIGINFO  = 0x04,
  CLONE_COW      = 0x08,
  CLONE_POPULATE = 0x10,
  CLONE_SHMEM    = 0x20,
  CLONE_PHYS     = 0x40,
} task_creation_flags_t;

#define TASK_MMCLONE_SHIFT 3
#define TASK_FLAG_UNDER_STATE_CHANGE  0x1

typedef uint32_t priority_t;
typedef uint32_t cpu_array_t;

#define CPU_AFFINITY_ALL_CPUS 0

struct __scheduler;
struct __task_ipc;
struct __userspace_events_data;
struct __task_ipc_priv;
struct __task_mutex_locks;
struct __task_sync_data;
struct __mutex;
struct __posix_stuff;

/* Per-task signal descriptors. */
struct __sighandlers;

typedef struct __signal_struct {
  spinlock_t lock;
  sigqueue_t sigqueue;
  sigset_t blocked,ignored,pending;
  struct __sighandlers *handlers;
} signal_struct_t;

/* task flags */
#define __TF_USPC_BLOCKED_BIT  0
#define __TF_UNDER_MIGRATION_BIT  1
#define __TF_EXITING_BIT  2
#define __TF_DISINTEGRATION_BIT  3
#define __TF_GCOLLECTED_BIT      4

typedef enum __task_privilege {
  TPL_KERNEL = 0,  /* Kernel task - the most serious level. */
  TPL_USER = 1,    /* User task - the least serious level */
} task_privelege_t;

typedef enum __task_flags {
  TF_USPC_BLOCKED=(1<<__TF_USPC_BLOCKED_BIT),/**< Block facility to change task's static priority outside the kernel **/
  TF_UNDER_MIGRATION=(1<<__TF_UNDER_MIGRATION_BIT), /**< Task is currently being migrated. Don't disturb. **/
  TF_EXITING=(1<<__TF_EXITING_BIT), /**< Task is exiting to avoid faults during 'sys_exit()' **/
  TF_DISINTEGRATING=(1<<__TF_DISINTEGRATION_BIT), /**< Task is being disintegrated **/
  TF_GCOLLECTED=(1<<__TF_GCOLLECTED_BIT) /**< Parent collected resources of this thread (threads only)*/
} task_flags_t;

/* Flags related to deferred userspace action processing */
#define DAF_CANCELLATION_PENDING 0x1 /**< Thread cancellation requested. */
#define DAF_EXIT_PENDING         0x2 /**< exit() is requested. */

struct __disintegration_descr_t;
typedef struct __uwork_data {
  list_head_t def_uactions;
  struct __disintegration_descr_t *disintegration_descr;

  /* Thread cancellation-related stuff. */
  uintptr_t destructor;
  int exit_value;
  uint8_t cancel_state,cancel_type;
  uint8_t flags;
} uworks_data_t;

/* Task that waits another task to exit. */
typedef struct __jointee {
  event_t e;
  list_node_t l;
  struct __task_struct *exiter;
  long exit_ptr;
} jointee_t;

typedef struct __tg_leader_private {
  countered_event_t ce;
  ulong_t num_threads;
  idx_allocator_t tid_allocator;
  mutex_t thread_mutex;
} tg_leader_private_t;

/* Abstract object for scheduling. */
typedef struct __task_struct {
  pid_t pid, ppid;
  tid_t tid;
  uid_t uid,gid;

  /* Scheduler-related stuff. */
  cpu_id_t cpu;
  task_state_t state;
  cpu_array_t cpu_affinity_mask;
  priority_t static_priority, priority, orig_priority;
  kernel_stack_t kernel_stack;
  uintptr_t ustack;
  uintptr_t ptd;
  
  union {
    rpd_t rpd;
    vmm_t *task_mm;
  };
  list_node_t pid_list;
  task_flags_t flags;

  spinlock_t lock;
  atomic_t refcount;

  /* Children/threads - related stuff. */
  struct __task_struct *group_leader;
  spinlock_t child_lock;
  list_head_t children,threads;
  list_node_t child_list;

  /* Scheduler-related stuff. */
  struct __scheduler *scheduler;
  void *sched_data;
  list_node_t migration_list;

  /* IPC-related stuff */
  struct __task_ipc *ipc;
  struct __task_ipc_priv *ipc_priv;

  struct __task_mutex_locks *active_locks;

  struct __task_sync_data *sync_data;

  /* Limits-related stuff. */
  task_limits_t *limits;
  task_privelege_t priv;
  /* Lock for protecting changing and outer access the following fields:
   * ipc,ipc_priv,limits
   */
  spinlock_t member_lock;

  struct __userspace_events_data *uspace_events;

  /* Task state events */
  task_events_t task_events;

  /* Signal-related stuff. */
  signal_struct_t siginfo;

  /* 'wait()'-related stuff. */
  jointee_t jointee;
  list_head_t jointed;
  countered_event_t *cwaiter;
  struct __task_struct *terminator;
  event_t reinc_event;

  struct __posix_stuff *posix_stuff;
  tg_leader_private_t *tg_priv;

  /* Userspace works-reated stuff. */
  uworks_data_t uworks_data;

  /* Arch-dependent context is located here */
  uint8_t arch_context[256];
} task_t;

#define LOCK_TASK_SIGNALS(t)  spinlock_lock(&(t)->siginfo.lock)
#define UNLOCK_TASK_SIGNALS(t)  spinlock_unlock(&(t)->siginfo.lock)

#define LOCK_TASK_SIGNALS_INT(t,_is)  spinlock_lock_irqsave(&(t)->siginfo.lock,(_is))
#define UNLOCK_TASK_SIGNALS_INT(t,_is)  spinlock_unlock_irqrestore(&(t)->siginfo.lock,(_is))

#define __ATTR_OFF  0  /**< Attribute is enabled **/
#define __ATTR_ON   1  /**< Attribute is disabled **/

typedef struct __task_attrs {
  uint8_t run_immediately;
} task_attrs_t;

#define __EXEC_ATTRS_COW       0x1
#define __EXEC_ATTRS_COPY_IPC  0x2

typedef struct __exec_attrs {
  uintptr_t stack,entrypoint,destructor,arg1,arg2;
  uintptr_t per_task_data;
  long flags;
} exec_attrs_t;

typedef struct __task_creation_attrs {
  task_attrs_t task_attrs;
  exec_attrs_t exec_attrs;
} task_creation_attrs_t;

static inline rpd_t *task_get_rpd(task_t *task)
{
  if (likely(task->priv != TPL_KERNEL))
    return &task->task_mm->rpd;

  return &task->rpd;
}

/**
 * @fn void initialize_task_subsystem(void)
 * @brief Initializes kernel task subsystem.
 *
 * This function must be invoked once during kernel boot to initialize
 * data structures related to kernel task management.
 */
void initialize_task_subsystem(void);

/**
 * @fn status_t kernel_thread(void (*fn)(void *), void *data)
 * @brief Create a new 'ready-to-run' kernel thread.
 *
 * This function creates a new thread that has kernel privileges
 * and shares all kernel memory mappings and symbols.
 * Note that unlike generic thread creation scheme, kernel threads
 * are created as 'runnable', which means that they preemt their
 * parents if their priorities are higher.
 * Kernel threads are created with default priority and default
 * scheduling policy as if they were regular user threads.
 *
 * @param fn - entrypoint of a new thread.
 * @param arg - argument to be passed to a new thread.
 * @param out_task - where to put the task descriptor of new task.
 *                   May be NULL if no descriptor required et all.
 * @return Return codes are identical to the 'create_task()' function.
 */
int kernel_thread(void (*fn)(void *), void *data, task_t **out_task);

/**
 * @fn status_t arch_setup_task_context(task_t *newtask,
 *                                      task_creation_flags_t flags,
 *                                      task_privelege_t priv)
 * @brief Setup arch-specific task context.
 *
 * This function is used for setting up arch-specific contexts for newly
 * created tasks.
 * @param newtask - Target task.
 * @param flags - Flags used for task creation (see 'create_task()' function)
 *                for all available flags.
 * @return - In case of success, zero is returned. Otherwise, a negated value
 *           of one of the standard error values is returned.
 * @param priv - Privilege level of target task.
 * @param attrs - Attributes of new task.
 */
int arch_setup_task_context(task_t *newtask,task_creation_flags_t flags,
                            task_privelege_t priv,task_t *parent,
                            task_creation_attrs_t *attrs);

/**
 * @fn arch_process_context_control(task_t *task,ulong_t cmd,ulong_t arg)
 * @brief Control task's arch-specific context.
 *
 * This function is used for task context manipulation in arch-specific manner.
 * @param task - Target task
 * @param cmd - Command.
 * @param arg - Command's argument.
 * @return - In case of success, zero is returned. Otherwise, a negated value
 *           of one of the standard error values is returned.
 * Note: See 'sys_process_control' for the list of available commands and their
 *       detailed symantics.
 */
int arch_process_context_control(task_t *task,ulong_t cmd,ulong_t arg);


int create_task(task_t *parent,ulong_t flags,task_privelege_t priv,
                task_t **new_task,task_creation_attrs_t *attrs);

/**
 * @fn status_t create_new_task(task_t *parent, task_t **t,
 *                              task_creation_flags_t flags,task_privelege_t priv)
 * @brief Create a new task (object for scheduling) without registering it
 *        in the scheduling subsystem.
 *
 * This function performs the same actions as 'create_task()', except that it
 * doesn't register new task in the scheduler.
 * See 'create_task()' for details.
 */
int create_new_task(task_t *parent,ulong_t flags,task_privelege_t priv,
                    task_t **t,task_creation_attrs_t *attrs);

/**
 * @fn void free_task_struct(task_t *task)
 * @brief Free task structure if it isn't referenced anymore.
 *
 * This function decrements reference counter of target task structure
 * and frees it in case the last reference was removed.
 * @Note This function only frees memory accupated by target structure.
 *       It doesn't free any resources (like memory space and other).
 */
void free_task_struct(task_t *task);

#define is_thread(task)  ((task)->group_leader && (task)->group_leader != (task))

void task_event_notify(ulong_t events);
int task_event_attach(struct __task_struct *target,
                      struct __task_struct *listener,
                           task_event_ctl_arg *ctl_arg);
void exit_task_events(struct __task_struct *target);

/* Default kernel threads flags. */
#define KERNEL_THREAD_FLAGS  (CLONE_MM)

#define TASK_INIT   0x80000000   /* This task is the NameServer i.e. 'init' */

#define set_task_flags(t,f) ((t)->flags |= (f))
#define check_task_flags(t,f) ((t)->flags & (f) )
#define set_and_check_task_flag(t,fb) (arch_bit_test_and_set(&(t)->flags,(fb)))
#define clear_task_flag(t,f) ((t)->flags &= ~(f))

#define ARCH_CTX_UWORS_SIGNALS_BIT_IDX      0
#define ARCH_CTX_UWORS_DISINT_REQ_BIT_IDX   1
#define ARCH_CTX_UWORS_DEF_ACTIONS_BIT_IDX  2
#define ARCH_CTX_UWORS_CANCEL_REQ_BIT_IDX   3

#define ARCH_CTX_UWORKS_SIGNALS_MASK  (1<<ARCH_CTX_UWORS_SIGNALS_BIT_IDX)
#define ARCH_CTX_UWORKS_DISINT_REQ_MASK  (1<<ARCH_CTX_UWORS_DISINT_REQ_BIT_IDX)
#define ARCH_CTX_UWORKS_CANCEL_REQ_MASK  (1<<ARCH_CTX_UWORS_CANCEL_REQ_BIT_IDX)

#define set_task_signals_pending(t)                                    \
  arch_set_uworks_bit( &(((task_t*)(t))->arch_context[0]),ARCH_CTX_UWORS_SIGNALS_BIT_IDX )

#define clear_task_signals_pending(t)             \
  arch_clear_uworks_bit( &(((task_t*)(t))->arch_context[0]),ARCH_CTX_UWORS_SIGNALS_BIT_IDX )

#define set_task_disintegration_request(t)      \
  arch_set_uworks_bit( &(((task_t*)(t))->arch_context[0]),ARCH_CTX_UWORS_DISINT_REQ_BIT_IDX )

#define clear_task_disintegration_request(t)      \
  arch_clear_uworks_bit( &(((task_t*)(t))->arch_context[0]),ARCH_CTX_UWORS_DISINT_REQ_BIT_IDX )

#define read_task_pending_uworks(t)             \
  arch_read_pending_uworks( &(((task_t*)(t))->arch_context[0]) )

#define __UNUSABLE_PTR (void *)0x007  /* Target pointer is not usable now. */

#define grab_task_struct(t) atomic_inc(&(t)->refcount)

void release_task_struct(struct __task_struct *t);

#endif
