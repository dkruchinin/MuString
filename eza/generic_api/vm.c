#include <eza/arch/types.h>
#include <eza/task.h>
#include <eza/errno.h>
#include <eza/vm.h>
#include <mm/page.h>
#include <mm/pfalloc.h>
#include <eza/spinlock.h>
#include <mm/pt.h>

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
  page_frame_iterator_t pfi;
  ITERATOR_CTX(page_frame,PF_ITER_INDEX) pfi_index_ctx;
  status_t r = 0;

  /* First, create kernel mapping for the most commin kernel areas. */
  r=arch_vm_map_kernel_area(task);
  if(r!=0) {
    return r;
  }

  list_for_each(&mand_list,it) {
    vm_range_t *area = list_entry(it,vm_range_t,l);

    mm_init_pfiter_index(&pfi,&pfi_index_ctx,
                         area->phys_addr/PAGE_SIZE,
                         area->phys_addr/PAGE_SIZE+area->num_pages-1);
    r = mm_map_pages(&task->page_dir,&pfi,area->virt_addr,
                     area->num_pages,area->map_flags);
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

  initialize_page_directory(&target->page_dir);

  /* Idle task or kernel thread ? Use main kernel pagetable. */
  if(orig == NULL || priv == TPL_KERNEL) {
    target->page_dir.entries = kernel_pt_directory.entries; 
    return 0;
  }

  /* TODO: [mt] Add normal MM sharing on task cloning. */
  if(flags & CLONE_MM) {
    /* Initialize new page directory. */
    target->page_dir.entries = orig->page_dir.entries;
    /* TODO: [mt] Increment regerence counters for all pages on VM cloning. */
    r = 0;
  } else {
    page_frame_t *page = alloc_page(AF_PGEN | AF_ZERO);
    if( page != NULL ) {
      target->page_dir.entries = pframe_to_virt(page);
      r = vm_map_mandatory_areas(target);
    } else {
      r = -ENOMEM;
    }
  }

  return r;
}
