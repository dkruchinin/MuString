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
#include <kernel/syscalls.h>
#include <ipc/gen_port.h>
#include <ipc/port.h>

#define SYS_PR_CTL_SET_ENTRYPOINT      0x0
#define SYS_PR_CTL_SET_STACK           0x1
#define SYS_PR_CTL_GET_ENTRYPOINT      0x2
#define SYS_PR_CTL_GET_STACK           0x3
#define SYS_PR_CTL_ADD_EVENT_LISTENER  0x4
#define SYS_PR_CTL_SET_PERTASK_DATA    0x5
#define SYS_PR_CTL_DISINTEGRATE_TASK   0x6  /* Very strong spell. */
#define SYS_PR_CTL_REINCARNATE_TASK    0x7  /* Another very strong spell. */

#define LOOKUP_ZOMBIES  0x1

typedef struct __disintegration_descr_t {
  ipc_gen_port_t *port;
  ipc_port_message_t *msg;
} disintegration_descr_t;

/* Data structure sent after performng 'SYS_PR_CTL_DISINTEGRATE_TASK' request. */
typedef struct __disintegration_req_packet {
  pid_t pid;
  ulong_t status;
} disintegration_req_packet_t;

#define __DR_EXITED  1 /* Target task exited before disintegrating itself. */

task_t *lookup_task(pid_t pid,ulong_t flags);

/* Default lookup for non-zombie tasks. */
#define pid_to_task(p)  lookup_task(p,0)

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

void zombify_task(task_t *target);
void spawn_percpu_threads(void);

#define EXITCODE(s,ec) ((s))

typedef enum {
  EF_DISINTEGRATE=0x1,
} exit_flags_t;

void do_exit(int code,ulong_t flags,ulong_t exitval);

void perform_disintegrate_work(void);

#endif
