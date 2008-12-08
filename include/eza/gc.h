#ifndef __GC_H__
#define  __GC_H__

#include <config.h>
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


task_t *gc_threads[CONFIG_NRCPUS][NUM_PERCPU_THREADS];

struct __gc_action;

typedef void (*gc_action_dtor_t)(struct __gc_action *action);
typedef void (*gc_actor_t)(void *data,ulong_t data_arg);
typedef void (*actor_t)(void *data);

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
void spawn_percpu_threads(void);

#define GC_TASK_RESOURCE  0x1

static inline void gc_init_action(gc_action_t *action,gc_actor_t actor,
                                  void *data,long_t data_arg)
{
  action->action=actor;
  action->data=data;
  list_init_node(&action->l);
  list_init_head(&action->data_list_head);
  action->type=0;
  action->data_arg=data_arg;
  action->dtor=NULL;
}

static inline void gc_put_action(gc_action_t *action)
{
  if(action->dtor) {
    action->dtor(action);
  }
}

#define GC_THREAD_IDX  1
#define MIGRATION_THREAD_IDX  0

#endif
