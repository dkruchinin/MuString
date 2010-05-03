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
 * (c) Copyright 2009,2010 Dmitry Gromada <gromada@jarios.org>
 *
 * Common declarations for process tracing
 */

#ifndef __PTRACE_H__
#define __PTRACE_H__

#include <arch/types.h>
#include <mstring/process.h>
#include <mstring/task.h>

enum {
  DETACH_NOLOCK = 0x01,
  DETACH_FORCE = 0x02
};

typedef ulong_t ptrace_mask_t;

#define process_trace_data(task)        ((task)->group_leader->pt_data)
#define event_is_traced(task, ev)       (process_trace_data(task).mask & (1 << ev))
#define process_tracer(task)            ((task)->group_leader->tracer)
#define ptrace_event(task)              ((task)->pt_data.event)

/* ptrace commands definition */
typedef enum __ptrace_cmd {
  PTRACE_ATTACH,
  PTRACE_TRACEME,
  PTRACE_DETACH,
  PTRACE_CONT,
  PTRACE_KILL,
  PTRACE_SINGLE_STEP,
  PTRACE_READ_MEM,
  PTRACE_WRITE_MEM,
  PTRACE_GET_SIGINFO,
  PTRACE_READ_INT_REGS,
  PTRACE_WRITE_INT_REGS,
  PTRACE_READ_FLP_REGS,
  PTRACE_WRITE_FLP_REGS,
  PTRACE_ADD_TRACED_EVENT,
  PTRACE_REMOVE_TRACED_EVENT,
  PTRACE_READ_EVENT_MSG
} ptrace_cmd_t;

/* ptrace events */
typedef enum __ptrace_event {
  PTRACE_EV_NONE,
  PTRACE_EV_STOPPED,
  PTRACE_EV_TRAP,
  PTRACE_EV_SYSENTER,
  PTRACE_EV_SYSEXIT,
  PTRACE_EV_FORK,
  PTRACE_EV_CLONE,
  PTRACE_EV_EXEC,
  PTRACE_EV_EXIT,
  /* !!! must be the last value */
  PTRACE_EV_LAST
} ptrace_event_t;

int ptrace_stop(ptrace_event_t event, ulong_t msg);
task_t *get_process_tracer(task_t *child);
bool ptrace_reparent(task_t *tracer, task_t *child);
int ptrace_detach(task_t *tracer, task_t *child, unsigned int flags);
static inline long ptrace_status(task_t *child);

static inline long ptrace_status(task_t *child)
{
  return (long)(child->pt_data.event << 16) | (child->last_signum << 8) |
                WSTAT_STOPPED;
}

static always_inline void set_ptrace_event(task_t *task, ptrace_event_t event)
{
  task->pt_data.event = event;
}

static inline bool task_stopped(task_t *task)
{
  return (task->state & (TASK_STATE_STOPPED | TASK_STATE_JUST_BORN)) != 0;
}

#endif /* __PTRACE_H__ */
