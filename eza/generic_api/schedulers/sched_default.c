#include <eza/arch/types.h>
#include <eza/resource.h>
#include <mm/pt.h>
#include <eza/kstack.h>
#include <eza/spinlock.h>
#include <eza/arch/current.h>
#include <ds/list.h>
#include <eza/scheduler.h>
#include <eza/errno.h>
#include <eza/sched_default.h>

static spinlock_t sched_lock;

status_t activate_task(task_t *task)
{
  if( task->state == TASK_STATE_JUST_BORN ||
    task->state == TASK_STATE_STOPPED ) {

    LOCK_TASK_STRUCT(task);
    task->state = TASK_STATE_RUNNABLE;
    UNLOCK_TASK_STRUCT(task);
    return 0;
  }
  return -EINVAL;
}

status_t deactivate_task(task_t *task, task_state_t state)
{
  if( task->state == TASK_STATE_RUNNABLE ||
    task->state == TASK_STATE_RUNNING ) {
    task_state_t prev_state = task->state;

    task->state = state;
    if( prev_state == TASK_STATE_RUNNABLE ) {
      /* Move targe task to the list of stopped tasks. */
    }
    UNLOCK_TASK_STRUCT(task);

    /* In case target task is running, we must reschedule it. */
    if( prev_state == TASK_STATE_RUNNING ) {
      reschedule_task(task);
    }

    return 0;
  }
  return -EINVAL;
}


status_t sched_do_change_task_state(task_t *task,task_state_t state)
{
  /* TODO: [mt] implement security check on task state change. */
  switch(state) {
    case TASK_STATE_RUNNABLE:
      return activate_task(task);
    case TASK_STATE_STOPPED:
      return deactivate_task(task,TASK_STATE_STOPPED);
    default:
      break;
  }

  return -EINVAL;
}

static cpu_id_t def_cpus_supported(void){
  return 2;
}

static void def_add_cpu(cpu_id_t cpu)
{
}

static void def_scheduler_tick(void)
{
  return 0;
}

static status_t def_add_task(task_t *task)
{
}

static void def_schedule(void)
{
}

static void def_reset(void)
{
}

static status_t def_change_task_state(task_t *task,task_state_t new_state)
{
  return 0;
}

static scheduler_t eza_default_scheduler = {
  .id = "Eza default scheduler",
  .cpus_supported = def_cpus_supported,
  .add_cpu = def_add_cpu,
  .scheduler_tick = def_scheduler_tick,
  .add_task = def_add_task,
  .schedule = def_schedule,
  .reset = def_reset,
  .change_task_state = def_change_task_state,
};

scheduler_t *get_default_scheduler(void)
{
  return &eza_default_scheduler;
}
