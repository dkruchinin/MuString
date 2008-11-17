#ifndef __GC_H__
#define  __GC_H__

#include <eza/arch/types.h>
#include <ds/list.h>
#include <eza/task.h>
#include <eza/smp.h>

#define NUM_MASTER_PERCPU_THREADS 1

#ifdef CONFIG_SMP
  #define NUM_SMP_PERCPU_THREADS  1
  #define NUM_PERCPU_THREADS  (NUM_MASTER_PERCPU_THREADS+ \
                               NUM_SMP_PERCPU_THREADS)
#else
  #define NUM_PERCPU_THREADS  NUM_MASTER_PERCPU_THREADS
#endif


task_t *gc_threads[NR_CPUS][NUM_PERCPU_THREADS];

struct __gc_action;

typedef void (*gc_action_dtor_t)(struct __gc_action *action);
typedef void (*gc_actor_t)(void *data,ulong_t data_arg);

typedef struct __gc_action {
  ulong_t type;
  gc_action_dtor_t dtor;
  gc_actor_t action;
  void *data;
  ulong_t data_arg;
  list_head_t data_list_head;
  list_node_t l;
} gc_action_t;

void initialize_gc(void);
gc_action_t *gc_allocate_action(gc_actor_t actor, void *data,
                                ulong_t data_arg);
void gc_schedule_action(gc_action_t *action);
void gc_free_action(gc_action_t *action);

#define GC_THREAD_IDX  0
#define MIGRATION_THREAD_IDX  1

#endif
