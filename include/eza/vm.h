#ifndef __VM_H__
#define  __VM_H__

#include <ds/list.h>
#include <eza/spinlock.h>
#include <mm/mm.h>
#include <eza/task.h>
#include <mlibc/types.h>

/* TODO: [mt] lock/unlock task VM via semaphores ! */
/*#define LOCK_TASK_VM(t)
#define UNLOCK_TASK_VM(t)

typedef struct __vm_range {
  list_node_t l;
  uintptr_t phys_addr,virt_addr,num_pages;
  uint_t map_proto;
  uint_t map_flags;
} vm_range_t;

bool vm_register_user_mandatory_area(vm_range_t *area);
void vm_unregister_user_mandatory_area(vm_range_t *area);

status_t vm_initialize_task_mm( task_t *orig, task_t *target,
                                task_creation_flags_t flags,
                                task_privelege_t priv );

status_t vm_map_mandatory_areas(task_t *task);

status_t arch_vm_map_kernel_area(task_t *task);*/

#endif
