/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * (c) Copyright 2006,2007,2008 MString Core Team <http://mstring.berlios.de>
 * (c) Copyright 2008 Michael Tsymbalyuk <mtzaurus@gmail.com>
 *
 * eza/generic_api/task.c: generic functions for dealing with task creation.
 */

#include <ds/list.h>
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
      list_init_node( &p->page_next );
      if(first == NULL) {
        first = p;
      } else {
        list_add2tail(&first->active_list,&p->page_next);
      }
    }
  }

  return first;
}

static status_t initialize_mm( task_t *orig, task_t *target,
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

static pid_t pid = 1;

status_t create_new_task( task_t *parent, task_t **t, task_creation_flags_t flags,task_privelege_t priv)
{
  task_t *task;
  page_frame_t *ts_page;
  status_t r = -ENOMEM;
  page_frame_t *stack_pages;
  kernel_task_data_t * td;
  pageaccs_list_pa_ctx_t pa_ctx;
  pageaccs_linear_pa_ctx_t l_ctx;

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

  /* Initialize task's MM. */
  r = initialize_mm(parent,task,flags);
  if( r != 0 ) {
    goto free_stack;
  }

  /* Prepare kernel stack. */
  /* TODO: [mt] Implement normal stack allocation. */
  stack_pages = alloc_stack_pages();
  if(stack_pages == NULL) {
    r = -ENOMEM;
    goto free_mm;
  }

  /* Map kernel stack. */
  pa_ctx.head = list_node_first(&stack_pages->active_list);
  pa_ctx.num_pages = KERNEL_STACK_PAGES;
  pageaccs_list_pa.reset(&pa_ctx);

  r = mm_map_pages( &task->page_dir, &pageaccs_list_pa,
                    task->kernel_stack.low_address, KERNEL_STACK_PAGES,
                    KERNEL_STACK_PAGE_FLAGS, &pa_ctx );
  if( r != 0 ) {
    goto free_stack_pages;
  }

  /* Map task struct into the stack area. */
  l_ctx.start_page = l_ctx.end_page = ts_page->idx;
  pageaccs_linear_pa.reset(&l_ctx);
  
  r = mm_map_pages( &task->page_dir, &pageaccs_linear_pa,
                    task->kernel_stack.low_address & KERNEL_STACK_MASK, 1,
                    KERNEL_STACK_PAGE_FLAGS, &l_ctx );
  if( r != 0 ) {
    goto unmap_stack_pages;
  }

  /* Initialize task system data. */
  initialize_task_system_data(td,cpu_id());
  /* TODO: [mt] Handle process PIDs properly. */
  task->pid = pid++;
  task->ppid = parent->pid;

  *t = task;
  return 0;
unmap_task_struct:
  /* TODO: Unmap task struct here. [mt] */
unmap_stack_pages:
  /* TODO: Unmap stack pages here. [mt] */
free_stack_pages:
  /* TODO: Free all stack pages here. [mt] */  
free_mm:
  /* TODO: Free mm here. [mt] */
free_stack:
  free_kernel_stack(task->kernel_stack.id);  
free_task:
  /* TODO: Free task struct page here. [mt] */
task_alloc_fault:
  *t = NULL;
  return r;
}

