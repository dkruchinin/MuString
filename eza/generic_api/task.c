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
#include <eza/smp.h>
#include <eza/kstack.h>
#include <eza/errno.h>
#include <mm/mm.h>
#include <mm/pfalloc.h>
#include <mm/mmap.h>
#include <eza/amd64/context.h>
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

  spinlock_initialize(&pid_array_lock);

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

#ifdef CONFIG_TEST
  /* To avoid kernel panic when exiting the first test task (its PID will
   * almost always be 1), we reserve PID 1.
   */
  __allocate_pid();
#endif

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

static status_t __alloc_pid_and_tid(task_t *parent,ulong_t flags,
                                    pid_t *ppid, tid_t *ptid,
                                    task_privelege_t priv)
{
  pid_t pid;
  tid_t tid;

  if( (flags & CLONE_MM) && priv != TPL_KERNEL ) {
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

static void __free_pid_and_tid(task_t *parent,pid_t pid, tid_t tid)
{
  __free_pid(pid);
  __free_tid(tid,parent->group_leader);
}

static void __add_to_parent(task_t *task,task_t *parent,ulong_t flags,
                            task_privelege_t priv)
{
  if( parent && parent->pid ) {
    task->ppid = parent->pid;

    if( (flags & CLONE_MM) && priv != TPL_KERNEL ) {
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
 
#if 0 /* [DEACTIVATED] */
static void __free_task_struct(task_t *task)
{
  memfree(task);
}
#endif 

void cleanup_thread_data(void *t,ulong_t arg)
{
  task_t *task=(task_t*)t;

  /* NOTE: Don't free task structure directly since it will
   * be probably processed via 'waitpid()' functionality
   */
  free_kernel_stack(task->kernel_stack.id);
}

static task_t *__allocate_task_struct(void)
{
  task_t *task=alloc_from_memcache(task_cache);

  if( task ) {
    memset(task,0,sizeof(*task));

    list_init_node(&task->pid_list);
    list_init_node(&task->child_list);
    list_init_node(&task->migration_list);

    list_init_head(&task->children);
    list_init_head(&task->threads);

    spinlock_initialize(&task->lock);
    spinlock_initialize(&task->child_lock);
    spinlock_initialize(&task->member_lock);

    task->flags = 0;
    task->group_leader=task;
    task->cpu_affinity_mask=ONLINE_CPUS_MASK;
  }
  return task;  
}

static status_t __setup_task_ipc(task_t *task,task_t *parent,ulong_t flags)
{
  status_t r;

  if( flags & CLONE_IPC ) {
    if( !parent->ipc ) {
      return -EINVAL;
    }
    task->ipc=parent->ipc;
    r=setup_task_ipc(task);
    if( !r ) {
      get_task_ipc(parent);
      dup_task_ipc_resources(task->ipc);
    }
    return r;
  } else {
    task->ipc=NULL;
    return setup_task_ipc(task);
  }
}

status_t create_new_task(task_t *parent,ulong_t flags,task_privelege_t priv, task_t **t)
{
  task_t *task;
  status_t r = -ENOMEM;
  page_frame_t *stack_pages;
  pid_t pid;
  tid_t tid;
  task_limits_t *limits;

  if( flags && !parent ) {
    return -EINVAL;
  }

  /* TODO: [mt] Add memory limit check. */
  /* goto task_create_fault; */  
  r=__alloc_pid_and_tid(parent,flags,&pid,&tid,priv);
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

  r = mmap(task->page_dir, task->kernel_stack.low_address, pframe_number(stack_pages),
           KERNEL_STACK_PAGES, MAP_READ | MAP_WRITE);
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

  r=__setup_task_ipc(task,parent,flags);
  if( r ) {
    goto free_limits;
  }

  task->uspace_events=allocate_task_uspace_events_data();
  if( !task->uspace_events ) {
    r=-ENOMEM;
    goto free_ipc;
  }

  /* Setup task's initial state. */
  task->state = TASK_STATE_JUST_BORN;
  task->cpu = cpu_id();

  /* Setup scheduler-related stuff. */
  task->scheduler = NULL;
  task->sched_data = NULL;
  task->flags = 0;

  __add_to_parent(task,parent,flags,priv);

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
  __free_pid_and_tid(parent,pid,tid);
task_create_fault:
  *t = NULL;
  return r;
}

