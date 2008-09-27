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

#define INVALID_PID  IA_INVALID_VALUE
#define NUM_PIDS  65536

/* PID-to-task_t translation hash stuff */
#define PID_HASH_LEVEL_SHIFT  9 /* Levels of PID-to-task cache. */
#define PID_HASH_LEVELS  (1 << PID_HASH_LEVEL_SHIFT)
#define PID_HASH_LEVEL_MASK  (PID_HASH_LEVELS-1)

/* Macros for locking task structure. */
#define LOCK_TASK_STRUCT(t) spinlock_lock(&t->lock)
#define UNLOCK_TASK_STRUCT(t) spinlock_unlock(&t->lock)

typedef uint32_t time_slice_t;

typedef enum __task_creation_flag_t {
  CLONE_MM = 0x1,
} task_creation_flags_t;

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

/* Abstract object for scheduling. */
typedef struct __task_struct {
  pid_t pid, ppid;
  cpu_id_t cpu;
  task_state_t state;
  cpu_array_t cpu_affinity;
  kernel_stack_t kernel_stack;
  page_directory_t page_dir;
  list_node_t pid_list;

  spinlock_t lock;

  /* Scheduler-related stuff. */
  struct __scheduler *scheduler;
  void *sched_data;

  struct __task_ipc *ipc;
  task_limits_t *limits;

  /* Arch-dependent context is located here */
  uint8_t arch_context[];
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

/**
 * @fn status_t create_task(task_t *parent,task_creation_flags_t flags,
 *                          task_privelege_t priv, task_t **new_task)
 * @brief Create a new task (object for scheduling).
 *
 * This routine is the kernel main routine for creating new threads.
 * The following conditions are met during kernel creation:
 *   - new thread will have its own unique PID;
 *   - the caling thread will be the parent of the new thread;
 *   - new thread will have its own kernel stack;
 *   - CPU ID of new thread will be equal to its parent's;
 *   - new thread's state will be 'TASK_STATE_JUST_BORN';
 *   - new thread will be registered in default scheduler and ready for
 *     scheduling;
 *   - new thread will have its own memory space unless the 'CLONE_MM' flag
 *     is specified;
 *
 * @param parent - Parent for new task. May be NULL, which means that no
 *                 parent-related information will be set in new thread.
 * @param flags - Task creation flags. Possible values are:
 *                CLONE_MM - new task will share its memory space with its
 *                           parent (suitable for creation 'threads').
 * @param priv - Privilege level of new task. Possible values are:
 *               TPL_KERNEL - the highest privilege level (kernel).
 *                            Used for kernel threads.
 *               TPL_USER - the least privilege level.
 *                          Used for creation all user processes.
 * @param new_task - Where to put pointer to the new task.
 * @return If new task was successfully created, this function returns 0.
 *         Otherwise, negation of the following error codes is returned:
 *         ENOMEM   No memory was available.
 */
status_t create_task(task_t *parent,task_creation_flags_t flags,task_privelege_t priv,
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
 status_t create_new_task(task_t *parent,task_creation_flags_t flags,task_privelege_t priv,
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

#endif

