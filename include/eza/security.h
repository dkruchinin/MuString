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
 * include/eza/security.h: security check procedures - related types and
 *                          prototypes.
 */

#ifndef __SECURITY_H__
#define  __SECURITY_H__

#include <eza/arch/types.h>
#include <eza/scheduler.h>
#include <eza/arch/page.h>
#include <eza/process.h>

typedef struct __security_operations_t {
  bool (*check_process_control)(task_t *target,ulong_t cmd, ulong_t arg);
  bool (*check_create_process)(task_creation_flags_t flags);
  bool (*check_scheduler_control)(task_t *target,ulong_t cmd, ulong_t arg);
  bool (*check_access_ioports)(task_t *target,ulong_t start_port,
                               ulong_t end_port); 
} security_operations_t;

extern security_operations_t *security_ops;

#define CAP_SCHED_POLICY 0

static inline bool capable(task_t *task, ulong_t capability )
{
  return true;
}

static inline bool trusted_task(task_t *task)
{
  return true;
}

#endif
