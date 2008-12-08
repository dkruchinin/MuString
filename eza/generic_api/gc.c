#include <eza/arch/types.h>
#include <eza/task.h>
#include <eza/smp.h>
#include <eza/kernel.h>
#include <eza/process.h>
#include <eza/gc.h>
#include <mm/slab.h>
#include <eza/gc.h>
#include <eza/arch/spinlock.h>
#include <eza/task.h>
#include <eza/scheduler.h>

static memcache_t *gc_actions_cache;
static list_head_t gc_tasklists[CONFIG_NRCPUS];
static spinlock_t tasklist_lock;
task_t *gc_threads[CONFIG_NRCPUS][NUM_PERCPU_THREADS];

#define get_gc_tasklist() &gc_tasklists[cpu_id()]

#define LOCK_TASKLIST() spinlock_lock(&tasklist_lock)
#define UNLOCK_TASKLIST() spinlock_unlock(&tasklist_lock)

static gc_action_t *__alloc_gc_action(void) {
  return alloc_from_memcache(gc_actions_cache);
}

static void __free_gc_action(struct __gc_action *action)
{
  memfree(action);
}

void initialize_gc(void)
{
  int i;

  for( i=0;i<CONFIG_NRCPUS;i++ ) {
    list_init_head(&gc_tasklists[i]);
  }

  gc_actions_cache = create_memcache( "GC action memcache", sizeof(gc_action_t),
                                      1, SMCF_PGEN);
  if( !gc_actions_cache ) {
    panic( "initialize_gc(): Can't create GC actions memcache !" );
  }

  spinlock_initialize(&tasklist_lock);
}

static void __gc_thread_logic(void *arg)
{
  while(1) {
    list_head_t *alist=get_gc_tasklist();
    list_head_t private;
    list_node_t *n;

    list_init_head(&private);

    LOCK_TASKLIST();
    if( !list_is_empty(alist) ) {
      list_move2head(&private,alist);
    }
    UNLOCK_TASKLIST();

    list_for_each(&private,n) {
      struct __gc_action *action=container_of(n,struct __gc_action,l);

      action->action(action->data,action->data_arg);
      if( action->dtor ) {
        action->dtor(action);
      }
    }

    sched_change_task_state(current_task(),TASK_STATE_SLEEPING);
  }
}

static actor_t __percpu_threads[NUM_PERCPU_THREADS] = {
  migration_thread,
  __gc_thread_logic,
};

void spawn_percpu_threads(void)
{
  int cpu,j;
  task_t **ts;

  cpu=cpu_id();
  ts=&gc_threads[cpu_id()][0];

  for(j=0;j<NUM_PERCPU_THREADS;j++) {
    if( kernel_thread(__percpu_threads[j],NULL, &ts[j]) || !ts[j] ) {
      panic( "Can't create system thread N %d for CPU %d !\n",
             j,cpu_id() );
    }
  }
}

gc_action_t *gc_allocate_action(gc_actor_t actor, void *data,
                                ulong_t data_arg)
{
  gc_action_t *action=__alloc_gc_action();
  if( action ) {
    gc_init_action(action,actor,data,data_arg);
    action->dtor=__free_gc_action;
  }
  return action;
}

void gc_free_action(gc_action_t *action)
{
  if( action->dtor == __free_gc_action ) {
    __free_gc_action(action);
  }
}

void gc_schedule_action(gc_action_t *action)
{
  list_head_t *alist=get_gc_tasklist();

  LOCK_TASKLIST();
  list_add2tail(alist,&action->l);
  UNLOCK_TASKLIST();

  if( gc_threads[cpu_id()][GC_THREAD_IDX] ) {
    sched_change_task_state(gc_threads[cpu_id()][GC_THREAD_IDX], TASK_STATE_RUNNABLE);
  } else {
    kprintf( KO_WARNING "gc_schedule_action(): scheduling GC action without GC thread !\n" );
  }
}
