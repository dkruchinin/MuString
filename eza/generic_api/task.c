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
#include <eza/sync.h>
#include <eza/signal.h>
#include <eza/sigqueue.h>
#include <eza/posix.h>

/* Available PIDs live here. */
static index_array_t pid_array;
static spinlock_t pid_array_lock;
static bool init_launched;

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
  idle=__allocate_pid();
  if(idle != 0) {
    panic( "initialize_task_subsystem(): Can't allocate PID for idle task ! (%d returned)\n",
           idle );
  }

  /* Reserve a PID for the init task. */
  idle=__allocate_pid();
  if(idle != 1) {
    panic( "initialize_task_subsystem(): Can't allocate PID for Init task ! (%d returned)\n",
           idle );
  }
  
  init_launched=false;
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

  /* Init task ? */
  if( flags & TASK_INIT ) {
    status_t r;

    LOCK_PID_ARRAY;
    if( !init_launched ) {
      *ppid=1;
      *ptid=1;
      init_launched=true;
      r=0;
    } else {
      r=-EINVAL;
    }
    UNLOCK_PID_ARRAY;
    return r;
  }

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
      task->group_leader=parent->group_leader;
      LOCK_TASK_CHILDS(task->group_leader);
      LOCK_TASK_STRUCT(task->group_leader);

      parent->group_leader->tg_priv->num_threads++;
      list_add2tail(&parent->group_leader->threads,
                    &task->child_list);
      UNLOCK_TASK_STRUCT(task->group_leader);
      UNLOCK_TASK_CHILDS(task->group_leader);
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

static task_t *__allocate_task_struct(ulong_t flags,task_privelege_t priv)
{
  task_t *task=alloc_pages_addr(1,AF_PGEN | AF_ZERO);

  if( task ) {
    if( !(flags & CLONE_MM) || priv == TPL_KERNEL ) {
      task->tg_priv=memalloc(sizeof(tg_leader_private_t));

      if( !task->tg_priv ) {
        free_pages_addr(task);
        return NULL;
      }

      memset(task->tg_priv,0,sizeof(tg_leader_private_t));
    }

    list_init_node(&task->pid_list);
    list_init_node(&task->child_list);
    list_init_node(&task->migration_list);

    list_init_head(&task->children);
    list_init_head(&task->threads);

    list_init_head(&task->task_events.my_events);
    list_init_head(&task->task_events.listeners);
    list_init_head(&task->jointed);

    event_initialize(&task->jointee.e);
    list_init_node(&task->jointee.l);

    spinlock_initialize(&task->lock);
    mutex_initialize(&task->child_lock);
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

static status_t __setup_task_sync_data(task_t *task,task_t *parent,ulong_t flags,
                                       task_privelege_t priv)
{
  if( flags & CLONE_MM ) {
    if( !parent->sync_data && (priv != TPL_KERNEL) ) {
      return -EINVAL;
    }
    if( parent->sync_data ) {
      task->sync_data=parent->sync_data;
      return dup_task_sync_data(parent->sync_data);
    }
  }

  task->sync_data=allocate_task_sync_data();
  return task->sync_data ? 0 : -ENOMEM;
}

static status_t __setup_signals(task_t *task,task_t *parent,ulong_t flags)
{
  sighandlers_t *shandlers=NULL;
  sigset_t blocked=0,ignored=DEFAULT_IGNORED_SIGNALS;

  if( flags & CLONE_SIGINFO ) {
    if( !parent->siginfo.handlers ) {
      return -EINVAL;
    }
    shandlers=parent->siginfo.handlers;
    atomic_inc(&shandlers->use_count);

    blocked=parent->siginfo.blocked;
    ignored=parent->siginfo.ignored;
  }

  if( !shandlers ) {
    shandlers=allocate_signal_handlers();
    if( !shandlers ) {
      return -ENOMEM;
    }
  }

  task->siginfo.blocked=blocked;
  task->siginfo.ignored=ignored;
  task->siginfo.pending=0;
  task->siginfo.handlers=shandlers;
  spinlock_initialize(&task->siginfo.lock);
  sigqueue_initialize(&task->siginfo.sigqueue,
                      &task->siginfo.pending);

  return 0;
}

static status_t __setup_posix(task_t *task,task_t *parent,
                              task_privelege_t priv,ulong_t flags)
{
  if( (flags & CLONE_MM) && priv != TPL_KERNEL ) {
    task->posix_stuff=parent->posix_stuff;
    atomic_inc(&task->posix_stuff->use_counter);
    return 0;
  } else {
    task->posix_stuff=allocate_task_posix_stuff();
    if( task->posix_stuff ) {
      return 0;
    }
    return -ENOMEM;
  }
}

status_t create_new_task(task_t *parent,ulong_t flags,task_privelege_t priv, task_t **t,
                         task_creation_attrs_t *attrs)
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

  task=__allocate_task_struct(flags,priv);
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
           KERNEL_STACK_PAGES, MAP_RW);
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

  r=__setup_task_sync_data(task,parent,flags,priv);
  if( r ) {
    goto free_ipc;
  }

  task->uspace_events=allocate_task_uspace_events_data();
  if( !task->uspace_events ) {
    r=-ENOMEM;
    goto free_sync_data;
  }

  r=__setup_signals(task,parent,flags);
  if( r ) {
    goto free_uevents;
  }

  r=__setup_posix(task,parent,priv,flags);
  if( r ) {
    goto free_signals;
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
free_signals:
  /* TODO: [mt] Free signals data properly. */
free_uevents:
  /* TODO: [mt] Free userspace events properly. */
free_sync_data:
  /* TODO: [mt] Deallocate task's sync data. */
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

