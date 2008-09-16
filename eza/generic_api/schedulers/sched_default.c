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
#include <mm/pagealloc.h>
#include <mlibc/assert.h>
#include <mlibc/string.h>

/* Our own scheduler. */
static struct __scheduler eza_default_scheduler;

/* Glabal per-CPU array. */
static eza_sched_cpudata_t *sched_cpu_data[EZA_SCHED_CPUS];
static spinlock_t cpu_data_lock;

#define LOCK_CPU_ARRAY() spinlock_lock(&cpu_data_lock)
#define UNLOCK_CPU_ARRAY() spinlock_unlock(&cpu_data_lock)

#define LOCK_CPU_SCHED_DATA(d) spinlock_lock(&d->lock)
#define UNLOCK_CPU_SCHED_DATA(d) spinlock_unlock(&d->lock)

#define CPU_SCHED_DATA() sched_cpu_data[cpu_id()]

static eza_sched_taskdata_t *allocate_task_sched_data(void)
{
  /* TODO: [mt] Allocate memory via slabs !!!  */
  return (eza_sched_taskdata_t *)__alloc_page(0,1);
}

static void free_task_sched_data(eza_sched_taskdata_t *data)
{
  /* TODO: [mt] Free structure via slabs ! */
}

static eza_sched_cpudata_t *allocate_cpu_sched_data(void) {
  /* TODO: [mt] Allocate memory via slabs !!!  */
  eza_sched_cpudata_t *cpudata = (eza_sched_cpudata_t *)__alloc_page(0,1);

  kprintf( "-- a CURRENT: %p, CPU data: %p\n", current_task(), cpudata );

  if( cpudata != NULL ) {
     initialize_cpu_sched_data(cpudata);
  }

  return cpudata;
}

static void free_cpu_sched_data(eza_sched_cpudata_t *data)
{
  /* TODO: [mt] Free structure via slabs ! */
}

static void initialize_cpu_sched_data(eza_sched_cpudata_t *cpudata)
{
  uint32_t arr,i;

  spinlock_initialize(&cpudata->lock, "Eza scheduler runqueue lock");
  cpudata->active_array = &cpudata->arrays[0];

  for( arr=0; arr<EZA_SCHED_NUM_ARRAYS; arr++ ) {
    eza_sched_prio_array_t *array = &cpudata->arrays[arr];
    list_head_t *lh = &array->queues[0];

    memset(&array->bitmap[0], EZA_SCHED_BITMAP_PATTERN, sizeof(array->bitmap));

    for( i=0; i<EZA_SCHED_TOTAL_PRIOS; i++ ) {
      list_init_head(lh);
      lh++;
    }
  }
}

static status_t setup_new_task(task_t *task)
{
  eza_sched_taskdata_t *sdata = allocate_task_sched_data();

  if( sdata == NULL ) {
    return -ENOMEM;
  }

  list_init_node(&sdata->runlist);

  LOCK_TASK_STRUCT(task);
  task->sched_data = sdata;
  task->scheduler = &eza_default_scheduler;
  UNLOCK_TASK_STRUCT(task);
  return 0;
}

/* Task must be locked while calling this function !
 */
static inline status_t activate_task(task_t *task, eza_sched_cpudata_t *sched_data)
{
  //  eza_sched_taskdata_t *cdata = (eza_sched_taskdata_t *)current_task()->sched_data;
  //  priority_t p = add_task_to_array(sched_data->active_array,task);

  task->state = TASK_STATE_RUNNABLE;
  //  kprintf( "++ NEW PRIO: %d, CURRENT: %p\n", p, current_task() );
  return 0;
}

/* Task must be locked while calling this function !
 */
static inline status_t deactivate_task(task_t *task, task_state_t state)
{
  /* In case target task is running, we must reschedule it. */
  if( task->state == TASK_STATE_RUNNING ) {
  //      reschedule_task(task);
  } else { /* TASK_STATE_RUNNABLE */
    
  }

  return 0;
}

static cpu_id_t def_cpus_supported(void){
  return EZA_SCHED_CPUS;
}

static status_t def_add_cpu(cpu_id_t cpu)
{
  eza_sched_cpudata_t *cpudata;
  status_t r;
 
  if(cpu >= EZA_SCHED_CPUS || sched_cpu_data[cpu] != NULL) {
    return -EINVAL;
  }

  cpudata = allocate_cpu_sched_data();
  if( cpudata == NULL ) {
    return -ENOMEM;
  }

  LOCK_CPU_ARRAY();

  if( sched_cpu_data[cpu] == NULL ) {
    sched_cpu_data[cpu] = cpudata;
    r = 0;
  } else {
    r = -EEXIST;
  }

  UNLOCK_CPU_ARRAY();

  if( r != 0 ) {
    free_cpu_sched_data( cpudata );
  }

  return r;
}

static void def_scheduler_tick(void)
{
}

static status_t def_add_task(task_t *task)
{
  cpu_id_t cpu = cpu_id();
  eza_sched_taskdata_t *sdata;

  if( sched_cpu_data[cpu] == NULL || task->state != TASK_STATE_JUST_BORN ) {
    return -EINVAL;
  }

  if( task->sched_data != NULL ) {
    kprintf( KO_WARNING "def_add_task(): Task being added already has a scheduler !\n" );
  }

  if( setup_new_task(task) != 0 ) {
    return -ENOMEM;
  }

  LOCK_TASK_STRUCT(task);

  sdata = (eza_sched_taskdata_t *)task->sched_data;
  sdata->static_priority = EZA_SCHED_DEF_NONRT_PRIO;
  sdata->priority = EZA_SCHED_DEF_NONRT_PRIO;
  task->cpu = cpu;

  UNLOCK_TASK_STRUCT(task);

  return 0;
}

static void def_schedule(void)
{
}

static void def_reset(void)
{
  int i;

  spinlock_initialize(&cpu_data_lock, "Eza def scheduler lock");

  for(i=0;i<EZA_SCHED_CPUS;i++) {
    sched_cpu_data[i] = NULL;
  }
}

static status_t def_change_task_state(task_t *task,task_state_t new_state)
{
  status_t r = -EINVAL;
  task_state_t prev_state;
  eza_sched_cpudata_t *sched_data = CPU_SCHED_DATA();

  kprintf( "** Changing task (%d) state to %d, %p\n", task->pid, new_state, sched_data );

  LOCK_TASK_STRUCT(task);
  prev_state = task->state;
  LOCK_CPU_SCHED_DATA(sched_data);

  switch(new_state) {
    case TASK_STATE_RUNNABLE:
      if( prev_state == TASK_STATE_JUST_BORN
	  || prev_state == TASK_STATE_STOPPED) {
	r = activate_task(task,sched_data);
      }
      break;
    case TASK_STATE_STOPPED:
        if( task->state == TASK_STATE_RUNNABLE
	    || task->state == TASK_STATE_RUNNING ) {
	  r = deactivate_task(task,TASK_STATE_STOPPED);
	}
       break;
    default:
      break;
  }

  UNLOCK_CPU_SCHED_DATA(sched_data);
  UNLOCK_TASK_STRUCT(task);

  return r;
}

static status_t def_setup_idle_task(task_t *task)
{
  eza_sched_taskdata_t *sdata;

  if( setup_new_task(task) != 0 ) {
    return -ENOMEM;
  }

  sdata = (eza_sched_taskdata_t *)task->sched_data;
  sdata->priority = 0;
  sdata->static_priority = EZA_SCHED_IDLE_TASK_PRIO;
  sdata->time_slice = 0;

  LOCK_TASK_STRUCT(task);
  task->state = TASK_STATE_RUNNING;
  task->scheduler = &eza_default_scheduler;
  task->sched_data = sdata;
  UNLOCK_TASK_STRUCT(task);

  return 0;
}

static struct __scheduler eza_default_scheduler = {
  .id = "Eza default scheduler",
  .cpus_supported = def_cpus_supported,
  .add_cpu = def_add_cpu,
  .scheduler_tick = def_scheduler_tick,
  .add_task = def_add_task,
  .schedule = def_schedule,
  .reset = def_reset,
  .change_task_state = def_change_task_state,
  .setup_idle_task = def_setup_idle_task,
};

scheduler_t *get_default_scheduler(void)
{
  return &eza_default_scheduler;
}
