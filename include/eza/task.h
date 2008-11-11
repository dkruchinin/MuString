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
 * include/eza/task.h: generic functions for dealing with task creation.
 */


#ifndef __TASK_H__
#define __TASK_H__ 

#include <eza/arch/types.h>
#include <eza/kstack.h>
#include <eza/arch/context.h>
#include <mlibc/index_array.h>
#include <mm/pt.h>
#include <eza/limits.h>

#define INVALID_PID  ((pid_t)~0) 
/* TODO: [mt] Manage NUM_PIDS properly ! */
#define NUM_PIDS  32768

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

/* Macros for locking task structure. */
#define LOCK_TASK_STRUCT(t) spinlock_lock(&t->lock)
#define UNLOCK_TASK_STRUCT(t) spinlock_unlock(&t->lock)

/*   */
#define LOCK_TASK_CHILDS(t) spinlock_lock(&t->child_lock)
#define UNLOCK_TASK_CHILDS(t) spinlock_unlock(&t->child_lock)

#define LOCK_TASK_MEMBERS(t) spinlock_lock(&t->member_lock)
#define UNLOCK_TASK_MEMBERS(t) spinlock_unlock(&t->member_lock)

typedef uint32_t time_slice_t;

typedef enum __task_creation_flag_t {
  CLONE_MM = 0x1,
  CLONE_IPC = 0x2,  
} task_creation_flags_t;

#define TASK_FLAG_UNDER_STATE_CHANGE  0x1

typedef enum __task_state {
  TASK_STATE_JUST_BORN = 0,
  TASK_STATE_RUNNABLE = 1,
  TASK_STATE_RUNNING = 2,
  TASK_STATE_SLEEPING = 3,
  TASK_STATE_STOPPED = 4,
  TASK_STATE_ZOMBIE = 5,
} task_state_t;

typedef uint32_t cpu_array_t;

#define CPU_AFFINITY_ALL_CPUS 0

struct __scheduler;
struct __task_ipc;
struct __userspace_events_data;
struct __task_ipc_priv;

/* Abstract object for scheduling. */
typedef struct __task_struct {
  pid_t pid, ppid;
  tid_t tid;
  cpu_id_t cpu;
  task_state_t state;
  cpu_array_t cpu_affinity;
  kernel_stack_t kernel_stack;
  page_directory_t page_dir;
  list_node_t pid_list;
  ulong_t flags;

  spinlock_t lock;

  /* Children/threads - related stuff. */
  struct __task_struct *group_leader;
  spinlock_t child_lock;
  list_head_t children,threads; 
  list_node_t child_list;

  /* Scheduler-related stuff. */
  struct __scheduler *scheduler;
  void *sched_data;

  /* IPC-related stuff */
  struct __task_ipc *ipc;
  struct __task_ipc_priv *ipc_priv;

  /* Limits-related stuff. */
  task_limits_t *limits;

  /* Lock for protecting changing and outer access the following fields:
   *   ipc,ipc_priv,limits
   */
  spinlock_t member_lock;

  struct __userspace_events_data *uspace_events;
  /* Arch-dependent context is located here */
  uint8_t arch_context[256];
} task_t;

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
 * @return Return codes are identical to the 'create_task()' function.
 */
status_t kernel_thread(void (*fn)(void *), void *data);

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
 */
status_t arch_setup_task_context(task_t *newtask,task_creation_flags_t flags,
                                 task_privelege_t priv);

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
status_t arch_process_context_control(task_t *task,ulong_t cmd,ulong_t arg);


status_t create_task(task_t *parent,ulong_t flags,task_privelege_t priv,
                     task_t **new_task);

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
 status_t create_new_task(task_t *parent,ulong_t flags,task_privelege_t priv,
                         task_t **t );

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

static inline bool is_thread( task_t *task )
{
  return (task->group_leader && task->group_leader != task);
}

void cleanup_thread_data(void *t);

#endif

