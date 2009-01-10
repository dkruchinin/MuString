#include <eza/arch/types.h>
#include <eza/task.h>
#include <eza/errno.h>
#include <eza/vm.h>
#include <mm/page.h>
#include <mm/pfalloc.h>
#include <mm/vmm.h>
#include <eza/spinlock.h>
#include <mm/mmap.h>

static SPINLOCK_DEFINE(mand_list_lock);
static LIST_DEFINE(mand_list);

#define LOCK_MAND_LIST spinlock_lock(&mand_list_lock)
#define UNLOCK_MAND_LIST spinlock_unlock(&mand_list_lock)

bool vm_register_user_mandatory_area(vm_range_t *area)
{
  LOCK_MAND_LIST;

  list_init_node(&area->l);
  list_add2tail(&mand_list,&area->l);

  UNLOCK_MAND_LIST;
  return true;
}

void vm_unregister_user_mandatory_area(vm_range_t *area)
{
  list_del(&area->l);
}

status_t vm_map_mandatory_areas(task_t *task)
{
  list_node_t *it;
  status_t r = 0;

  /* First, create kernel mapping for the most commin kernel areas. */
  r=arch_vm_map_kernel_area(task);
  if(r!=0) {
    return r;
  }

  list_for_each(&mand_list,it) {
    vm_range_t *area = list_entry(it,vm_range_t,l);

    r = mmap_kern(area->virt_addr, area->phys_addr >> PAGE_WIDTH,
                  area->num_pages, area->map_proto, area->map_flags);
    if(r!=0) {
      break;
    }
  }

  if(r!=0) {
    /* TODO: [mt] Unmap memory on failure. */
  }

  return r;
}

status_t vm_initialize_task_mm( task_t *orig, task_t *target,
                                task_creation_flags_t flags,
                                task_privelege_t priv )
{
  status_t r;

  /* Idle task or kernel thread ? Use main kernel pagetable. */
  if(orig == NULL || priv == TPL_KERNEL) {
    ptable_rpd_clone(&target->rpd, &kernel_rpd);
    return 0;
  }

  /* TODO: [mt] Add normal MM sharing on task cloning. */
  if(flags & CLONE_MM) {
    /* Initialize new page directory. */
    ptable_rpd_clone(&target->rpd, &orig->rpd);
    /* TODO: [mt] Increment regerence counters for all pages on VM cloning. */
    r = 0;
  } else {    
    r = ptable_rpd_initialize(&target->rpd);
    if (r)
      return r;
    
    r = vm_map_mandatory_areas(target);
  }

  return r;
}
