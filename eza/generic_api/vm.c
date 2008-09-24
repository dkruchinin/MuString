#include <eza/arch/types.h>
#include <eza/task.h>
#include <eza/errno.h>

status_t initialize_task_mm( task_t *orig, task_t *target,
                               task_creation_flags_t flags )
{
  status_t r;

  initialize_page_directory(&target->page_dir);

  if(orig == NULL) {
    target->page_dir.entries = kernel_pt_directory.entries; 
    return 0;
  }

  /* TODO: [mt] Add normal MM sharing on task cloning. */
  if(flags & CLONE_MM) {
    /* Initialize new page directory. */
    target->page_dir.entries = orig->page_dir.entries;
    r = 0;
  } else {
    r = -EINVAL;
  }

  return r;
}
