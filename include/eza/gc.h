#ifndef __GC_H__
#define  __GC_H__

#include <eza/arch/types.h>
#include <ds/list.h>

struct __gc_action;

typedef void (*gc_action_dtor_t)(struct __gc_action *action);
typedef void (*gc_actor_t)(void *data);

typedef struct __gc_action {
  gc_action_dtor_t dtor;
  gc_actor_t action;
  void *data;
  list_node_t l;  
} gc_action_t;

void initialize_gc(void);
gc_action_t *gc_allocate_action(gc_actor_t actor, void *data);
void gc_schedule_action(gc_action_t *action);

#endif
