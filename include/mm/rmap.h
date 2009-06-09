#ifndef __RMAP_H__
#define __RMAP_H__

#include <config.h>
#include <ds/list.h>
#include <mm/page.h>
#include <mm/vmm.h>
#include <sync/rwsem.h>
#include <mstring/types.h>

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
  };
} rmap_group_entry_t;

static inline struct __memobj *memobj_from_page(page_frame_t *page)
{
  struct __memobj *memobj;

  if (unlikely(!page->rmap_anon)) {
    memobj = NULL;
  }
  else if (page->flags & (PF_COW | PF_SHARED)) {
    memobj = page->rmap_shared->memobj;
  }
  else {
    memobj = page->rmap_anon->memobj;
  }
  
  return memobj;
}

void rmap_subsystem_initialize(void);
int rmap_register_anon(page_frame_t *page, vmm_t *vmm, uintptr_t addr);
int rmap_unregister_anon(page_frame_t *page, vmm_t *vmm, uintptr_t addr);
int rmap_register_shared_entry(page_frame_t *page, vmm_t *vmm, uintptr_t addr);
int rmap_register_shared(memobj_t *memobj, page_frame_t *page, vmm_t *vmm, uintptr_t addr);
int rmap_unregister_shared(page_frame_t *page, vmm_t *vmm, uintptr_t addr);
int rmap_register_mapping(memobj_t *memobj, page_frame_t *page, vmm_t *vmm, uintptr_t address);
int rmap_unregister_mapping(page_frame_t *page, vmm_t *vmm, uintptr_t address);

#endif /* __RMAP_H__ */
