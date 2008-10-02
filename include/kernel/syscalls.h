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
 */

#ifndef __K_SYSCALLS_H__
#define  __K_SYSCALLS_H__

#include <eza/arch/types.h>
#include <eza/task.h>

/* Syscalls identificators. */
#define SC_GET_PID         0
#define SC_CREATE_TASK     1
#define SC_TASK_CONTROL    2

/**
 * @fn status_t sys_get_pid(void)
 * @return PID of the calling process.
 */
status_t sys_get_pid(void);


/**
 * @fn status_t sys_create_task(task_creation_flags_t flags)
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
 * @param flags - Task creation flags. Possible values are:
 *                CLONE_MM - new task will share its memory space with its
 *                           parent (suitable for creation 'threads').
 * @return If new task was successfully created, this function returns
 *         the PID of the new task.
 *         Otherwise, negation of the following error codes is returned:
 *         ENOMEM   No memory was available.
 */
status_t sys_create_task(task_creation_flags_t flags);


/**
 * @fn status_t sys_task_control( pid_t pid, ulong_t cmd, ulong_t arg);
 * @brief Main function for controlling tasks.
 *
 * @param target - Task to control
 * @param cmd - Command. Possible commands are:
 *   SYS_PR_CTL_SET_ENTRYPOINT
 *     Set entrypoint for a newly created task to @a arg. Target task will start
 *     execution from target entrypoint.
 *     This command can be applied only to tasks whose state is 'TASK_STATE_JUST_BORN'.
 *     Otherwise, -EINVAL is returned.
 *
 *   SYS_PR_CTL_SET_STACK
 *     Set top of user stack for a newly created task to @a arg. Target task will
 *     start execution using this new stack.
 *     This command can be applied only to tasks whose state is 'TASK_STATE_JUST_BORN'.
 *     Otherwise, -EINVAL is returned.
 *
 *   SYS_PR_CTL_GET_ENTRYPOINT
 *     Get target process's entrypoint.
 *     This command can be applied only to tasks whose state is 'TASK_STATE_JUST_BORN'.
 *     Otherwise, -EINVAL is returned.
 *
 *   SYS_PR_CTL_GET_STACK
 *     Get top of user stack of target task.
 *     This command can be applied only to tasks whose state is 'TASK_STATE_JUST_BORN'.
 *     Otherwise, -EINVAL is returned.
 *
 * @param arg - Command's argument.
 * @param - Return value. Rreturn values are command-specific.
 * In general, for 'setters', this function returns zero on successful completion,
 * otherwise it returns on of the following errors (negated):
 *    EINVAL - invalid command/argument;
 *    ESRCH - invalid task was specified;
 *    EACCESS - calling process is not allowed to perform the command
 *              requested;
 *    EFAULT - argument points to insufficient address in userspace;
 *    
 */
status_t sys_task_control( pid_t pid, ulong_t cmd, ulong_t arg);

#endif
