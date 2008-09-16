#ifndef __SCHED_DEFAULT_H__
#define  __SCHED_DEFAULT_H__

#include <eza/scheduler.h>
#include <eza/arch/types.h>
#include <eza/smp.h>
#include <ds/list.h>
#include <eza/bits.h>
#include <eza/arch/bits.h>

#define eza_sched_type_t uint64_t
#define EZA_SCHED_PRIO_GRANULARITY 64

#define EZA_SCHED_RT_TASKS_WIDTH 2
#define EZA_SCHED_NONRT_TASKS_WIDTH 2
#define EZA_SCHED_TOTAL_WIDTH (EZA_SCHED_RT_TASKS_WIDTH+EZA_SCHED_NONRT_TASKS_WIDTH)

#define EZA_SCHED_NONRT_PRIOS (EZA_SCHED_NONRT_TASKS_WIDTH*EZA_SCHED_PRIO_GRANULARITY)
#define EZA_SCHED_RT_PRIOS (EZA_SCHED_RT_TASKS_WIDTH*EZA_SCHED_PRIO_GRANULARITY)

#define EZA_SCHED_NONRT_PRIO_BASE EZA_SCHED_RT_PRIOS
#define EZA_SCHED_RT_PRIO_BASE 0
#define EZA_SCHED_NONRT_MIN_PRIO EZA_SCHED_NONRT_PRIO_BASE

#define EZA_SCHED_NONRT_PRIORITY_MAX (EZA_SCHED_NONRT_PRIO_BASE+EZA_SCHED_NONRT_PRIOS-1)
#define EZA_SCHED_RT_MAXPRIO (EZA_SCHED_RTPRIOS-1)
#define EZA_SCHED_TOTAL_PRIOS (EZA_SCHED_NONRT_PRIORITY_MAX+1)

#define EZA_SCHED_PRIORITY_MAX (EZA_SCHED_NONRT_PRIORITY_MAX)
#define EZA_SCHED_IDLE_TASK_PRIO (EZA_SCHED_PRIORITY_MAX+1)
#define EZA_SCHED_DEF_NONRT_PRIO (EZA_SCHED_NONRT_MIN_PRIO + EZA_SCHED_NONRT_PRIOS/2)

#define EZA_SCHED_CPUS MAX_CPUS
#define EZA_SCHED_NUM_ARRAYS 1
#define EZA_SCHED_BITMAP_PATTERN 0x00

#define SET_BITMAP_BIT(array,bit) \
  set_and_test_bit_mem_64(&array->bitmap[0],bit)
#define RESET_BITMAP_BIT(array,bit) \
  reset_and_test_bit_mem_64(&array->bitmap[0],bit)

typedef uint32_t priority_t;

typedef struct __eze_sched_taskdata {
  priority_t static_priority, priority;
  time_slice_t time_slice;
  list_node_t runlist;
  task_t *task;
} eza_sched_taskdata_t;

typedef struct __eza_sched_prio_array {
  eza_sched_type_t bitmap[EZA_SCHED_TOTAL_WIDTH];
  list_head_t queues[EZA_SCHED_TOTAL_PRIOS];
} eza_sched_prio_array_t;

typedef struct __eza_sched_cpudata {
  spinlock_t lock;
  eza_sched_prio_array_t *active_array;
  eza_sched_prio_array_t arrays[EZA_SCHED_NUM_ARRAYS];
  list_head_t non_active_tasks;
} eza_sched_cpudata_t;

static eza_sched_taskdata_t *allocate_task_sched_data(void);
static void free_task_sched_data(eza_sched_taskdata_t *data);
static eza_sched_cpudata_t *allocate_cpu_sched_data(void);
static void free_cpu_sched_data(eza_sched_cpudata_t *data);
static status_t setup_new_task(task_t *task);
static void initialize_cpu_sched_data(eza_sched_cpudata_t *queue);

/* 64-bit bits-related stuff. */
/* Array must be locked prior to calling this function !
 */
static inline priority_t __add_task_to_array(eza_sched_prio_array_t *array,task_t *task)
{
  eza_sched_taskdata_t *sched_data = (eza_sched_taskdata_t *)task->sched_data;
  priority_t prio = sched_data->priority;

  kprintf( "** TASK INDEX: %d\n", prio );
  SET_BITMAP_BIT(array,prio);
  list_add2tail(&array->queues[prio],&sched_data->runlist);

  return prio;
}

#endif
