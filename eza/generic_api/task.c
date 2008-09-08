
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
#include <eza/list.h>

int initialize_stack_system_area(kernel_task_data_t *t)
{
  uintptr_t task_vaddr = t->task.kernel_stack.low_address & KERNEL_STACK_MASK;
  page_idx_t tidx = virt_to_pframe_id(t);

  kprintf( "== Stack base: 0x%X, Page ID: 0x%X\n", task_vaddr, tidx );
//  mm_map_pages( &(t->task.page_dir));
//  kprintf( "== Vaddr of page 0x%X is 0x%X\n", pidx, vaddr );
//  page_idx_t pidx = mm_pin_virtual_address( &task->page_dir,
//                                            task->kernel_stack.low_address & KERNEL_STACK_MASK );
//  if( pidx != INVALID_PAGE_IDX ) {
//    void *vaddr = pframe_id_to_virt(pidx);

//    kprintf( "== Vaddr of page 0x%X is 0x%X\n", pidx, vaddr );

    return 0;
//  }
//  return -EFAULT;
}

int setup_task_kernel_stack(task_t *task)
{
  int r = allocate_kernel_stack(&task->kernel_stack);

  if( r == 0 ) {
    r = mm_map_pages( &task->page_dir, NULL,
                      task->kernel_stack.low_address, KERNEL_STACK_PAGES,
                      KERNEL_STACK_PAGE_FLAGS, NULL );
  }
  return r;
}

void initialize_task_system_data(kernel_task_data_t *task, cpu_id_t cpu)
{
  task->system_data.cpu_id = cpu;
  task->system_data.irq_num = 0;
}

static page_frame_t *alloc_stack_pages(void)
{
  int i;
  page_frame_t *p, *first;

  first = NULL;
  for( i = 0; i < KERNEL_STACK_PAGES; i++ ) {
    p = alloc_page(GENERIC_KERNEL_PAGE,0);
    if( p == NULL ) {
      return NULL;
    } else {
      init_list_head( &p->active_list );
      if(first == NULL) {
        first = p;
      } else {
        list_add_tail(&p->active_list,&first->active_list);
      }
    }
  }

  return first;
}

static status_t initialize_pagedir( task_t *orig, task_t *target,
                                     task_creation_flags_t flags )
{
  status_t r;

  /* TODO: Add normal MM sharing on task cloning. */
  if(flags & CLONE_MM) {
    /* Initialize new page directory. */
      initialize_page_directory(&target->page_dir);
      target->page_dir.entries = orig->page_dir.entries;
      r = 0;
  } else {
    r = -EINVAL;
  }

  return r;
}

static pid_t pid = 1;

status_t do_fork(void *arch_ctx, task_creation_flags_t flags)
{
  task_t *new_task;
  status_t r;
  task_t *parent = current_task();

  r = create_new_task(parent,&new_task,flags);
  if(r == 0) {
    r = arch_copy_process(parent,new_task,arch_ctx,flags);
    if(r == 0) {
      /* New task is ready. */
    } else {
  
    }
  }

  return r;
}

status_t create_new_task( task_t *parent, task_t **t, task_creation_flags_t flags )
{
  task_t *task;
  page_frame_t *ts_page;
  status_t r = -ENOMEM;
  page_frame_t *stack_pages;
  kernel_task_data_t * td;
  pageaccs_list_pa_ctx_t pa_ctx;

  /* TODO: Add memory limit check. */
  ts_page = alloc_page(GENERIC_KERNEL_PAGE,1);
  if( ts_page == NULL ) {
    goto task_alloc_fault;
  }

  td = (kernel_task_data_t *)pframe_to_virt(ts_page);
  task = &td->task;

  /* Create kernel stack for the new task. */
  r = allocate_kernel_stack(&task->kernel_stack) != 0;
  if( r != 0 ) {
    goto free_task;
  }

  r = initialize_pagedir(parent,task,flags);

  /* Prepare kernel stack. */
  stack_pages = alloc_stack_pages();
  if(stack_pages == NULL) {
    goto free_stack;
  }

  /* Map kernel stack. */
  pa_ctx.head = stack_pages;
  pa_ctx.num_pages = KERNEL_STACK_PAGES;
  pageaccs_list_pa.reset(&pa_ctx);

  r = mm_map_pages( &task->page_dir, &pageaccs_list_pa,
                    task->kernel_stack.low_address, KERNEL_STACK_PAGES,
                    KERNEL_STACK_PAGE_FLAGS, &pa_ctx );
  if( r != 0 ) {
    goto free_stack_pages;
  }

  /* Initialize task system data. */
  initialize_task_system_data(td,cpu_id());

  /* Now perform arch-specific task creation manipulations. */

  kprintf( ">>>> Stack: high address: 0x%X, Low address: 0x%X <<<<\n",
           task->kernel_stack.high_address, task->kernel_stack.low_address );

  task->pid = pid++;
  task->ppid = parent->pid;

  *t = task;
  return 0;
free_stack_pages:
  /* TODO: Free all stack pages here. */  
free_stack:
  free_kernel_stack(task->kernel_stack.id);  
free_task:
  /* TODO: Free task struct page here. */
task_alloc_fault:
  *t = NULL;
  return r;
}

