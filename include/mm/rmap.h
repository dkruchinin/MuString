#ifndef __RMAP_H__
#define __RMAP_H__

#include <config.h>
#include <ds/list.h>
#include <mm/page.h>
#include <mm/vmm.h>
#include <eza/rwsem.h>
#include <mlibc/types.h>

struct __memobj;
typedef struct __rmap_group_head {
  list_head_t head;
  struct __memobj *memobj;
  int num_locations;  
} rmap_group_head_t;

typedef struct __rmap_group_entry {
  vmm_t *vmm;
  uintptr_t addr;
  union {
    list_node_t node;
    struct __memobj *memobj;
  }
} rmap_group_entry_t;

void rmap_subsystem_initialize(void);
void rmap_initialize(rmap_t *rmap, struct __memobj *owner);

#endif /* __RMAP_H__ */
