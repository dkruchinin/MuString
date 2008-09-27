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
 * include/eza/process.h: base system process-related functions.
 */


#ifndef __PROCESS_H__
#define __PROCESS_H__

#include <eza/task.h>
#include <eza/arch/types.h>

#define SYS_PR_CTL_SET_ENTRYPOINT 0x0
#define SYS_PR_CTL_SET_STACK 0x1
#define SYS_PR_CTL_GET_ENTRYPOINT 0x2
#define SYS_PR_CTL_GET_STACK 0x3

task_t *pid_to_task(pid_t pid);

/**
 * @fn status_t do_process_control(task_t *target,ulong_t cmd, ulong_t arg)
 * @brief Kernel entrypoint to main function for controlling tasks.
 *
 * @param target - Target task
 * @param cmd - Command. See 'sys_task_control()' for supported commands.
 * @param arg - Command's argument.
 * @return - Return values are the same as for 'sys_task_control()' except
 * the following error codes:
 *
 *    ESRCH - invalid task was specified;
 *    EACCESS - calling process is not allowed to perform the command
 *              requested;
 */
status_t do_task_control(task_t *target,ulong_t cmd, ulong_t arg);

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

status_t sys_create_task(task_creation_flags_t flags);

status_t sys_get_pid(void);

#endif
