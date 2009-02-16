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
 * include/eza/scheduler.h: contains main types and prototypes for the generic
 *                          scheduler layer.
 *
 */

#ifndef __SCHEDULER_H__
#define __SCHEDULER_H__ 

#include <config.h>
#include <eza/arch/types.h>
#include <eza/kstack.h>
#include <eza/spinlock.h>
#include <eza/arch/current.h>
#include <ds/list.h>
#include <eza/arch/preempt.h>

struct __task_struct;
typedef int32_t pid_t;
typedef uint64_t tid_t;

#define SCHED_PRIO_MAX 128

/* Handler for extra check during the scheduling step.
 * If it returns true, target task will be rescheduled,
 * otherwise - it won't.
 */
typedef bool (*deferred_sched_handler_t)(void *data);

typedef enum __task_state {
  TASK_STATE_RUNNING = 0x1,
  TASK_STATE_RUNNABLE = 0x2,
  TASK_STATE_JUST_BORN = 0x4,
  TASK_STATE_SLEEPING = 0x8,   /**< Interruptible sleep. **/
  TASK_STATE_STOPPED = 0x10,
  TASK_STATE_SUSPENDED = 0x20,  /**< Non-interruptible sleep. **/
  TASK_STATE_ZOMBIE = 0x8000,
} task_state_t;

#define __ALL_TASK_STATE_MASK  0x3F  /**< All possible task states exclude zombies */

/* Abstract scheduler. */
typedef struct __scheduler {
  const char *id;
  list_node_t l;
  cpu_id_t (*cpus_supported)(void);
  int (*add_cpu)(cpu_id_t cpu);
  void (*scheduler_tick)(void);
  int (*add_task)(struct __task_struct *task);
  int (*del_task)(struct __task_struct *task);
  int (*move_task_to_cpu)(struct __task_struct *task,cpu_id_t cpu);
  void (*schedule)(void);
  void (*reset)(void);
  int (*change_task_state)(struct __task_struct *task,task_state_t  state,ulong_t mask);
  int (*change_task_state_deferred)(struct __task_struct *task,task_state_t state,
                                         deferred_sched_handler_t handler,
                                         void *data,ulong_t mask);
  int (*setup_idle_task)(struct __task_struct *task);
  int (*scheduler_control)(struct __task_struct *task, ulong_t cmd,ulong_t arg);
} scheduler_t;

/* Main scheduling policies. */
typedef enum __sched_discipline {
  SCHED_RR = 0,  /* Round-robin discipline. */
  SCHED_FIFO = 1, /* FIFO discipline. */
  SCHED_OTHER = 2, /* Default 'O(1)-like' discipline. */
} sched_discipline_t;

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

extern struct __task_struct *idle_tasks[CONFIG_NRCPUS];

int sched_change_task_state_mask(struct __task_struct *task,ulong_t state,
                                      ulong_t mask);
int sched_change_task_state_deferred_mask(struct __task_struct *task,ulong_t state,
                                               deferred_sched_handler_t handler,void *data,
                                               ulong_t appl_state);

#define sched_change_task_state(task,state)     \
  sched_change_task_state_mask((task),(state),__ALL_TASK_STATE_MASK)

#define sched_change_task_state_deferred(task,state,hander,data)        \
  sched_change_task_state_deferred_mask((task),(state),(hander),(data),      \
                                        __ALL_TASK_STATE_MASK)

int sched_add_task(struct __task_struct *task);
int sched_del_task(struct __task_struct *task);
int sched_setup_idle_task(struct __task_struct *task);
int sched_add_cpu(cpu_id_t cpu);
int sched_move_task_to_cpu(struct __task_struct *task,cpu_id_t cpu);
void update_idle_tick_statistics(scheduler_cpu_stats_t *stats);

extern scheduler_t *get_default_scheduler(void);

void schedule(void);

/* Macros that deal with resceduling needs. */
#define sched_set_current_need_resched() arch_sched_set_current_need_resched()
#define sched_reset_current_need_resched() arch_sched_reset_current_need_resched()

#define set_task_need_resched(t)  arch_sched_set_cpu_need_resched((t)->cpu)

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
#define SYS_SCHED_CTL_GET_CPU   0xC
#define SYS_SCHED_CTL_SET_CPU   0xD

#define SCHEDULER_MAX_COMMON_IOCTL SYS_SCHED_CTL_SET_CPU

int sys_yield(void);
long sys_scheduler_control(pid_t pid, ulong_t cmd, ulong_t arg);
long do_scheduler_control(struct __task_struct *task, ulong_t cmd, ulong_t arg);
long sleep(ulong_t ticks);

#ifdef CONFIG_SMP

#define CPU_TASK_REBALANCE_DELAY  HZ
void migration_thread(void *data);

#endif

static inline void grab_task_struct(struct __task_struct *t)
{
}

static inline void release_task_struct(struct __task_struct *t)
{
}

static inline void set_cpu_online(cpu_id_t cpu, uint32_t online)
{
  cpu_id_t mask = 1 << cpu;

  if( online ) {
    online_cpus |= mask;
  } else {
    online_cpus &= ~mask;
  }
}

static inline bool is_cpu_online(cpu_id_t cpu)
{
  return (online_cpus & (1 << cpu)) ? true : false;
}

#define cpu_affinity_ok(task,c) ( ((task)->cpu_affinity_mask & (1<<(c))) && is_cpu_online((c)) )

#define activate_task(t) sched_change_task_state((t),TASK_STATE_RUNNABLE)
#define stop_task(t) sched_change_task_state((t),TASK_STATE_STOPPED)
#define suspend_task(t) sched_change_task_state((t),TASK_STATE_SUSPENDED)

#endif

