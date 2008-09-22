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

#include <eza/arch/types.h>
#include <eza/scheduler.h>

typedef enum __task_creation_flag_t {
  CLONE_MM = 0x1,
} task_creation_flags_t;

#define SYS_PR_CTL_SET_ENTRYPOINT 0x0
#define SYS_PR_CTL_SET_STACK 0x1
#define SYS_PR_CTL_GET_ENTRYPOINT 0x2
#define SYS_PR_CTL_GET_STACK 0x3

task_t *pid_to_task(pid_t pid);

status_t sys_process_control( pid_t pid, ulong_t cmd, ulong_t arg);
status_t sys_create_process(task_creation_flags_t flags);

#endif
