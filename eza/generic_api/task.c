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

#include <ds/iterator.h>
#include <ds/list.h>
#include <eza/task.h>
#include <mm/pt.h>
#include <eza/smp.h>
#include <eza/kstack.h>
#include <eza/errno.h>
#include <mm/mm.h>
#include <mm/pfalloc.h>
#include <eza/amd64/context.h>
#include <mlibc/kprintf.h>
#include <eza/arch/scheduler.h>
#include <eza/arch/types.h>
#include <eza/kernel.h>
#include <eza/arch/task.h>
#include <mlibc/index_array.h>
#include <eza/spinlock.h>
#include <eza/arch/preempt.h>
#include <eza/vm.h>
#include <eza/limits.h>
#include <ipc/ipc.h>
#include <eza/uinterrupt.h>
#include <mm/slab.h>

/* Available PIDs live here. */
static index_array_t pid_array;
static spinlock_t pid_array_lock;
static memcache_t *task_cache;

/* Macros for dealing with PID array locks. */
#define LOCK_PID_ARRAY spinlock_lock(&pid_array_lock)
#define UNLOCK_PID_ARRAY spinlock_unlock(&pid_array_lock)

void initialize_process_subsystem(void);

static pid_t __allocate_pid(void)
{
  LOCK_PID_ARRAY;
  pid_t pid = index_array_alloc_value(&pid_array);
  UNLOCK_PID_ARRAY;

  return pid;
}

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
  idle = __allocate_pid();
  if(idle != 0) {
    panic( "initialize_task_subsystem(): Can't allocate PID for idle tasks ! (%d returned)",
           idle );
  }

  task_cache = create_memcache( "Task struct memcache", sizeof(task_t),
                                2, SMCF_PGEN);
  if( !task_cache ) {
    panic( "initialize_task_subsystem(): Can't create the task struct memcache !" );
  }

  initialize_process_subsystem();
}

static void __free_pid(pid_t pid)
{
  LOCK_PID_ARRAY;
  index_array_free_value(&pid_array,pid);
  UNLOCK_PID_ARRAY;
}

static page_frame_t *alloc_stack_pages(void)
{
  page_frame_t *p;

  p = alloc_pages(KERNEL_STACK_PAGES, AF_PGEN);
  return p;
}

static tid_t __allocate_tid(task_t *group_leader)
{
  static tid_t tid=1;
  return tid++;
}

static void __free_tid(tid_t tid,task_t *group_leader)
{
}

static status_t __alloc_pid_and_tid(task_t *parent,
                                    task_creation_flags_t flags,
                                    pid_t *ppid, tid_t *ptid)
{
  pid_t pid;
  tid_t tid;

  if( flags & CLONE_MM ) {
    if( !parent ) {
      return -EINVAL;
    }
    pid=parent->pid;
    tid=GENERATE_TID(pid,__allocate_tid(parent->group_leader));
  } else {
    pid = __allocate_pid();
    if( pid == INVALID_PID ) {
      return -ENOMEM;
    }
    tid=pid;
  }

  *ppid=pid;
  *ptid=tid;
  return 0;
}

static void __free_pid_and_tid(task_t *parent,
                               task_creation_flags_t flags,
                               pid_t pid, tid_t tid)
{
  __free_pid(pid);
  __free_tid(tid,parent->group_leader);
}

static void __add_to_parent(task_t *task,task_t *parent,
                            task_creation_flags_t flags)
{
  if( parent && parent->pid ) {
    task->ppid = parent->pid;

    if( flags & CLONE_MM ) {
      LOCK_TASK_CHILDS(task->group_leader);
      list_add2tail(&parent->group_leader->threads,
                    &task->child_list);
      UNLOCK_TASK_CHILDS(task->group_leader);

      task->group_leader=parent->group_leader;
    } else {
      LOCK_TASK_CHILDS(parent);
      list_add2tail(&parent->children,
                    &task->child_list);
      UNLOCK_TASK_CHILDS(parent);
    }
  } else {
    task->ppid=0;
  }
}

static task_t *__allocate_task_struct(void)
{
  task_t *task=alloc_from_memcache(task_cache);
  
  if( task ) {
    list_init_node(&task->pid_list);
    list_init_node(&task->child_list);

    list_init_head(&task->children);
    list_init_head(&task->threads);
    spinlock_initialize(&task->lock, "Task lock");
    spinlock_initialize(&task->child_lock, "Task child lock");
    task->flags = 0;
    task->group_leader=task;
  }
  return task;  
}

status_t create_new_task(task_t *parent,task_creation_flags_t flags,task_privelege_t priv, task_t **t)
{
  task_t *task;
  status_t r = -ENOMEM;
  page_frame_t *stack_pages;
  page_frame_iterator_t pfi;
  ITERATOR_CTX(page_frame, PF_ITER_INDEX) pfi_idx_ctx;
  pid_t pid;
  tid_t tid;
  task_limits_t *limits;
  task_ipc_t *ipc;

  /* TODO: [mt] Add memory limit check. */
  /* goto task_create_fault; */  
  r=__alloc_pid_and_tid(parent,flags,&pid,&tid);
  if( r ) {
    goto task_create_fault;
  }

  task=__allocate_task_struct();
  if( !task ) {
    goto free_pid;
  }

  task->pid=pid;
  task->tid=tid;

  /* Create kernel stack for the new task. */
  r = allocate_kernel_stack(&task->kernel_stack);
  if( r != 0 ) {
    goto free_task;
  }

  /* Initialize task's MM. */
  r = vm_initialize_task_mm(parent,task,flags,priv);
  if( r != 0 ) {
    goto free_stack;
  }

  /* Prepare kernel stack. */
  /* TODO: [mt] Implement normal stack allocation. */
  if(!(stack_pages = alloc_stack_pages())) {
    r = -ENOMEM;
    goto free_mm;
  }

  /* Map kernel stack. */
  mm_init_pfiter_index(&pfi, &pfi_idx_ctx,
                       pframe_number(stack_pages),
                       pframe_number(stack_pages) + KERNEL_STACK_PAGES - 1);
  r = mm_map_pages( &task->page_dir, &pfi,
                    task->kernel_stack.low_address, KERNEL_STACK_PAGES,
                    MAP_KERNEL | MAP_RW);
  if( r != 0 ) {
    goto free_stack_pages;
  }

  r=-ENOMEM;
  /* Setup limits. */
  limits = allocate_task_limits();
  if(limits==NULL) {
    goto free_stack_pages;
  } else {
    set_default_task_limits(limits);
    task->limits = limits;
  }

  /* Setup IPC stuff. */
  ipc = allocate_task_ipc();
  if(ipc==NULL) {
    goto free_limits;
  } else {
    task->ipc = ipc;
  }

  task->uspace_events=allocate_task_uspace_events_data();
  if( !task->uspace_events ) {
    goto free_ipc;
  }

  /* Setup task's initial state. */
  task->state = TASK_STATE_JUST_BORN;
  task->cpu = cpu_id();

  /* Setup scheduler-related stuff. */
  task->scheduler = NULL;
  task->sched_data = NULL;
  task->flags = 0;

  __add_to_parent(task,parent,flags);

  *t = task;
  return 0;
free_ipc:
  /* TODO: [mt] deallocate task's IPC structure. */
free_limits:
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
  __free_pid_and_tid(parent,flags,pid,tid);
task_create_fault:
  *t = NULL;
  return r;
}

