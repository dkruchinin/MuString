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
#include <mlibc/index_array.h>
#include <eza/spinlock.h>


/* Available PIDs live here. */
static index_array_t pid_array;
static spinlock_t pid_array_lock;

/* Macros for dealing with PID array locks. */
#define LOCK_PID_ARRAY spinlock_lock(&pid_array_lock)
#define UNLOCK_PID_ARRAY spinlock_unlock(&pid_array_lock)

static pid_t allocate_pid(void);
static void free_pid(pid_t pid);

void initialize_process_subsystem(void);

void initialize_task_subsystem(void)
{
  pid_t idle;

  spinlock_initialize(&pid_array_lock,"PID array spinlock");

  if( !index_array_initialize(&pid_array, NUM_PIDS) ) {
    panic( "initialize_task_subsystem(): Can't initialize PID index array !" );
  }

  /* Sanity check: allocate PID 0 for idle tasks, so the next available PID
   * will be 1 (init).
   */
  idle = allocate_pid();
  if(idle != 0) {
    panic( "initialize_task_subsystem(): Can't allocate PID for idle tasks ! (%d returned)",
           idle );
  }

  initialize_process_subsystem();
}

static pid_t allocate_pid(void)
{
  LOCK_PID_ARRAY;
  pid_t pid = index_array_alloc_value(&pid_array);
  UNLOCK_PID_ARRAY;
  return pid;
}

static void free_pid(pid_t pid)
{
  LOCK_PID_ARRAY;
  index_array_free_value(&pid_array,pid);
  UNLOCK_PID_ARRAY;
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

status_t create_new_task(task_t *parent, task_t **t, task_creation_flags_t flags,task_privelege_t priv)
{
  task_t *task;
  page_frame_t *ts_page;
  status_t r = -ENOMEM;
  page_frame_t *stack_pages;
  pageaccs_list_pa_ctx_t pa_ctx;
  pageaccs_linear_pa_ctx_t l_ctx;
  pid_t pid, ppid;

  /* TODO: [mt] Add memory limit check. */
  /* goto task_create_fault; */  

  /* First, try to allocate a PID. */
  pid = allocate_pid();
  if( pid == INVALID_PID ) {
    goto task_create_fault;
  }

  ts_page = alloc_page(GENERIC_KERNEL_PAGE,1);
  if( ts_page == NULL ) {
    goto free_pid;
  }

  task = pframe_to_virt(ts_page);

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
  pa_ctx.head = stack_pages;
  pa_ctx.num_pages = KERNEL_STACK_PAGES;
  pageaccs_list_pa.reset(&pa_ctx);

  r = mm_map_pages( &task->page_dir, &pageaccs_list_pa,
                    task->kernel_stack.low_address, KERNEL_STACK_PAGES,
                    KERNEL_STACK_PAGE_FLAGS, &pa_ctx );
  if( r != 0 ) {
    goto free_stack_pages;
  }

  if(parent != NULL) {
    ppid = parent->pid;
  } else {
    ppid = 0;
  }

  task->pid = pid;
  task->ppid = ppid; 

  list_init_node(&task->pid_list);
  spinlock_initialize(&task->lock, "Task lock");

  /* Setup task's initial state. */
  task->state = TASK_STATE_JUST_BORN;

  *t = task;
  return 0;
  /* TODO: Unmap stack pages here. [mt] */
free_stack_pages:
  /* TODO: Free all stack pages here. [mt] */  
free_mm:
  /* TODO: Free mm here. [mt] */
free_stack:
  free_kernel_stack(task->kernel_stack.id);  
free_task:
  /* TODO: Free task struct page here. [mt] */
free_pid:
  free_pid(pid);
task_create_fault:
  *t = NULL;
  return r;
}

