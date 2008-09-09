#include <eza/task.h>
#include <mm/pt.h>
#include <eza/smp.h>
#include <eza/kstack.h>
#include <eza/errno.h>
#include <mm/pagealloc.h>
#include <eza/amd64/context.h>
#include <mlibc/kprintf.h>
#include <eza/arch/scheduler.h>
#include <eza/arch/types.h>
#include <eza/kernel.h>
#include <eza/pageaccs.h>
#include <eza/arch/task.h>

extern task_t *kthread1;

status_t create_task(task_t *parent,task_creation_flags_t flags,task_privelege_t priv,
                     task_t **newtask)
{
  task_t *new_task;
  status_t r;

  r = create_new_task(parent,&new_task,flags,priv);
  if(r == 0) {
    r = arch_setup_task_context(new_task,flags,priv);
    if(r == 0) {
      /* New task is ready. */
      kthread1 = new_task;
    } else {
    }
  }

  if( newtask != NULL ) {
    if(r<0) {
      new_task = NULL;
    }
    *newtask = new_task;
  }

  return r;
}

