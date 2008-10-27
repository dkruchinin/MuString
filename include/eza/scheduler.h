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
 * include/eza/scheduler.h: contains main types and prototypes for the generic
 *                          scheduler layer.
 *
 */

#ifndef __SCHEDULER_H__
#define __SCHEDULER_H__ 

#include <eza/arch/types.h>
#include <eza/resource.h>
#include <eza/kstack.h>
#include <eza/spinlock.h>
#include <eza/arch/current.h>
#include <ds/list.h>
#include <eza/arch/preempt.h>
#include <eza/task.h>

/* Handler for extra check during the scheduling step.
 * If it returns true, target task will be rescheduled,
 * otherwise - it won't.
 */
typedef bool (*lazy_sched_handler_t)(void *data);

/* Abstract scheduler. */
typedef struct __scheduler {
  const char *id;
  list_node_t l;
  cpu_id_t (*cpus_supported)(void);
  status_t (*add_cpu)(cpu_id_t cpu);
  void (*scheduler_tick)(void);
  status_t (*add_task)(task_t *task);
  void (*schedule)(void);
  void (*reset)(void);
  status_t (*change_task_state)(task_t *task,task_state_t state);
  status_t (*change_task_state_lazy)(task_t *task,task_state_t state,
                                     lazy_sched_handler_t handler,void *data);
  status_t (*setup_idle_task)(task_t *task);
  status_t (*scheduler_control)(task_t *task, ulong_t cmd,ulong_t arg);
} scheduler_t;

#define GRAB_SCHEDULER(s)
#define RELEASE_SCHEDULER(s)

typedef struct __scheduler_cpu_stats {
  uint64_t task_switches, idle_switches, idle_ticks;
  uint64_t active_tasks,sleeping_tasks;
} scheduler_cpu_stats_t;

void initialize_scheduler(void);

bool sched_register_scheduler(scheduler_t *sched);
bool sched_unregister_scheduler(scheduler_t *sched);
scheduler_t *sched_get_scheduler(const char *name);

void sched_timer_tick(void);

void idle_loop(void);

extern task_t *idle_tasks[MAX_CPUS];

status_t sched_change_task_state(task_t *task,task_state_t state);
status_t sched_change_task_state_lazy(task_t *task,task_state_t state,
                                      lazy_sched_handler_t handler,void *data);
status_t sched_add_task(task_t *task);
status_t sched_setup_idle_task(task_t *task);
status_t sched_add_cpu(cpu_id_t cpu);
void update_idle_tick_statistics(scheduler_cpu_stats_t *stats);

extern scheduler_t *get_default_scheduler(void);

void schedule(void);

/* Macros that deal with resceduling needs. */
extern void arch_sched_set_current_need_resched(void);
extern void arch_sched_reset_current_need_resched(void);

#define sched_set_current_need_resched() arch_sched_set_current_need_resched()
#define sched_reset_current_need_resched() arch_sched_reset_current_need_resched()

#define SYS_SCHED_CTL_SET_POLICY  0x0
#define SYS_SCHED_CTL_GET_POLICY  0x1
#define SYS_SCHED_CTL_SET_PRIORITY  0x2
#define SYS_SCHED_CTL_GET_PRIORITY  0x3
#define SYS_SCHED_CTL_MONOPOLIZE_CPU  0x4
#define SYS_SCHED_CTL_DEMONOPOLIZE_CPU  0x5
#define SYS_SCHED_CTL_SET_AFFINITY_MASK  0x6
#define SYS_SCHED_CTL_GET_AFFINITY_MASK  0x7
#define SYS_SCHED_CTL_SET_MAX_TIMISLICE  0x8
#define SYS_SCHED_CTL_GET_MAX_TIMISLICE  0x9
#define SYS_SCHED_CTL_GET_STATE 0xA
#define SYS_SCHED_CTL_SET_STATE 0xB

#define SCHEDULER_MAX_COMMON_IOCTL SYS_SCHED_CTL_SET_STATE

status_t sys_yield(void);
status_t sys_scheduler_control(pid_t pid, ulong_t cmd, ulong_t arg);

static inline void grab_task_struct(task_t *t)
{
}

static inline void release_task_struct(task_t *t)
{
}

#endif

