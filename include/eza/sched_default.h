#ifndef __SCHED_DEFAULT_H__
#define  __SCHED_DEFAULT_H__

#include <eza/scheduler.h>
#include <eza/arch/types.h>
#include <eza/smp.h>

#define EZA_SCHED_PRIO_GRANULARITY 64

#define EZA_SCHED_RT_TASKS_WIDTH 2
#define EZA_SCHED_NONRT_TASKS_WIDTH 2

#define EZA_SCHED_NONRT_PRIOS (EZA_SCHED_NONRT_TASKS_WIDTH*EZA_SCHED_PRIO_GRANULARITY)
#define EZA_SCHED_RT_PRIOS (EZA_SCHED_RT_TASKS_WIDTH*EZA_SCHED_PRIO_GRANULARITY)

#define EZA_SCHED_NONRT_PRIO_BASE EZA_SCHED_RT_PRIOS
#define EZA_SCHED_RT_PRIO_BASE 0
#define EZA_SCHED_NONRT_MIN_PRIO EZA_SCHED_NONRT_PRIO_BASE

#define EZA_SCHED_NONRT_PRIORITY_MAX (EZA_SCHED_NONRT_PRIO_BASE+EZA_SCHED_NONRT_PRIOS-1)
#define EZA_SCHED_RT_MAXPRIO (EZA_SCHED_RTPRIOS-1)

#define EZA_SCHED_PRIORITY_MAX (EZA_SCHED_NONRT_PRIORITY_MAX)
#define EZA_SCHED_IDLE_TASK_PRIO (EZA_SCHED_PRIORITY_MAX+1)
#define EZA_SCHED_DEF_NONRT_PRIO (EZA_SCHED_NONRT_MIN_PRIO + EZA_SCHED_NONRT_PRIOS/2)

#define EZA_SCHED_CPUS MAX_CPUS

typedef struct __eze_sched_taskdata {
  priority_t static_priority, priority;
  time_slice_t time_slice;
} eza_sched_taskdata_t;

typedef struct __cpu_runqueue {
} cpu_runqueue_t;

typedef struct __eza_sched_cpudata {
  
} eza_sched_cpudata_t;

static eza_sched_taskdata_t *allocate_task_sched_data(void);
static void free_task_sched_data(eza_sched_taskdata_t *data);
static eza_sched_cpudata_t *allocate_cpu_sched_data(void);
static void free_cpu_sched_data(eza_sched_cpudata_t *data);
static status_t setup_new_task(task_t *task);

#endif
