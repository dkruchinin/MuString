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
 * (c) Copyright 2006,2007,2008 MString Core Team <http://mstring.jarios.org>
 * (c) Copyright 2008 Michael Tsymbalyuk <mtzaurus@gmail.com>
 * (c) Copyright 2010 MadTirra <madtirra@jarios.org>
 *
 * mstring/generic_api/task.c: generic functions for dealing with task creation.
 */

#include <ds/list.h>
#include <mstring/task.h>
#include <mstring/smp.h>
#include <mstring/kstack.h>
#include <mstring/errno.h>
#include <mm/slab.h>
#include <arch/task.h>
#include <sync/spinlock.h>
#include <mstring/limits.h>
#include <ipc/ipc.h>
#include <mstring/uinterrupt.h>
#include <mstring/sync.h>
#include <mstring/signal.h>
#include <mstring/sigqueue.h>
#include <mstring/posix.h>
#include <arch/scheduler.h>
#include <mstring/types.h>
#include <mstring/process.h>
#include <mstring/namespace.h>
#include <config.h>

static bool init_launched;

/* Macros for dealing with PID array locks. */
/* NOTE if CONFIG_ENABLE_NS is not set, we have
   only one namespace */
#define LOCK_PID_ARRAY(ns) spinlock_lock(&ns->pid_array_lock)
#define UNLOCK_PID_ARRAY(ns) spinlock_unlock(&ns->pid_array_lock)


void initialize_process_subsystem(void);

static void __free_pid(struct namespace * ns, pid_t pid)
{
  if (ns){
    LOCK_PID_ARRAY(ns);
    idx_free(&ns->pid_array, pid);
    UNLOCK_PID_ARRAY(ns);
  }
}

static pid_t __allocate_pid(struct namespace * ns)
{
  pid_t pid;
  if (ns){
    LOCK_PID_ARRAY(ns);
    pid = idx_allocate(&ns->pid_array);
    UNLOCK_PID_ARRAY(ns);
  }
  return pid;
}

void initialize_task_subsystem(void)
{
  pid_t idle;

  /* Sanity check: allocate PID 0 for idle tasks, so the next available PID
   * will be 1 (init).
   */
  idle = __allocate_pid(root_ns);
  if(idle != 0) {
    panic( "initialize_task_subsystem(): Can't allocate PID for idle task ! (%d returned)\n",
           idle );
  }

  /* Reserve a PID for the init task. */
  idle = __allocate_pid(root_ns);
  if(idle != 1) {
    panic( "initialize_task_subsystem(): Can't allocate PID for Init task ! (%d returned)\n",
           idle );
  }

  init_launched = false;
  initialize_process_subsystem();
}


static void __free_pid_and_tid(task_t *task)
{
  if (!is_thread(task)){
    __free_pid(task->namespace->ns, task->pid);
    if (!is_kernel_thread(task))
      task->namespace->ns->pid_count--;
  }
  else {
    LOCK_TASK_STRUCT(task->group_leader);
    idx_free(&task->group_leader->tg_priv->tid_allocator,task->tid);
    UNLOCK_TASK_STRUCT(task->group_leader);
  }
  task->pid = INVALID_PID;
  task->tid = 0;
}

static int __alloc_pid_and_tid(task_t *task,task_t *parent,ulong_t flags,
                               task_privelege_t priv)
{
  pid_t pid = INVALID_PID;
  tid_t tid = 0;
  int r =- ENOMEM;

  /* Allocate PID first. */
  if( flags & TASK_INIT ) {
    LOCK_PID_ARRAY(root_ns);
    if( !init_launched ) {
      pid = 1;
      root_ns->pid_count++;
      init_launched = true;
    } else {
      r =- EINVAL;
    }
    UNLOCK_PID_ARRAY(root_ns);
  } else {
    if( (flags & CLONE_MM) && priv != TPL_KERNEL ) {
      pid = parent->pid;
      LOCK_TASK_STRUCT(parent);
      tid = idx_allocate(&parent->group_leader->tg_priv->tid_allocator);
      UNLOCK_TASK_STRUCT(parent);
    } else {
      pid = __allocate_pid(task->namespace->ns);
      if (pid != IDX_INVAL)
        root_ns->pid_count++;
    }
  }

  if( pid == IDX_INVAL || tid == IDX_INVAL ) {
    return ERR(r);
  }


  task->pid = pid;
  task->tid = tid;
  return 0;
}

static int __add_to_parent(task_t *task,task_t *parent,ulong_t flags,
                            task_privelege_t priv)
{
  int r=0;

  if( parent && parent->pid ) {
    if( (flags & CLONE_MM) && priv != TPL_KERNEL ) {
      task->ppid = parent->ppid;  /* parent is set to the parent of group leader */
      task->group_leader = parent->group_leader;
      LOCK_TASK_CHILDS(task->group_leader);
      LOCK_TASK_STRUCT(task->group_leader);

      if( !(task->group_leader->flags & TF_EXITING) ) {
        parent->group_leader->tg_priv->num_threads++;
        list_add2tail(&parent->group_leader->threads,
                      &task->child_list);
      } else {
        r=-EAGAIN;
      }
      UNLOCK_TASK_STRUCT(task->group_leader);
      UNLOCK_TASK_CHILDS(task->group_leader);
    } else {
      task->ppid = parent->pid;
      LOCK_TASK_CHILDS(parent);
      list_add2tail(&parent->children,
                    &task->child_list);
      UNLOCK_TASK_CHILDS(parent);
    }
  } else {
    task->ppid = 0;
  }
  return ERR(r);
}

void cleanup_thread_data(gc_action_t *action)
{
  task_t *task=(task_t*)action->data;

  free_kernel_stack(task->kernel_stack.id);
  release_task_struct(task);
}

static tg_leader_private_t *__allocate_tg_data(task_privelege_t priv)
{
  tg_leader_private_t *d = memalloc(sizeof(*d));

  if( d ) {
    memset(d,0,sizeof(*d));
    if( priv != TPL_KERNEL ) {
       if(idx_allocator_init(&d->tid_allocator, CONFIG_TASK_THREADS_LIMIT_MAX) ) {
         memfree(d);
         return NULL;
       } else {
         /* Reserve zero index for the main thread. */
         idx_reserve(&d->tid_allocator,0);
         mutex_initialize(&d->thread_mutex);
       }
    }
  }
  return d;
}

static task_t *__allocate_task_struct(ulong_t flags,task_privelege_t priv)
{
  task_t *task=alloc_pages_addr(1, MMPOOL_KERN | AF_ZERO);

  if( task ) {
    if( !(flags & CLONE_MM) || priv == TPL_KERNEL ) {
      task->tg_priv=__allocate_tg_data(priv);
      if( !task->tg_priv ) {
        free_pages_addr(task, 1);
        return NULL;
      }
    }

    list_init_head(&task->children);
    list_init_head(&task->threads);
    list_init_head(&task->trace_children);

    list_init_head(&task->jointed);

    spinlock_initialize(&task->lock, "Task");
    spinlock_initialize(&task->child_lock, "Child");
    spinlock_initialize(&task->member_lock, "Task members");

    atomic_set(&task->refcount,TASK_INITIAL_REFCOUNT); /* One extra ref is for 'wait()' */
    task->flags = 0;
    task->group_leader=task;
    task->cpu_affinity_mask = ONLINE_CPUS_MASK;

    task->uworks_data.cancel_state = PTHREAD_CANCEL_ENABLE;
    task->uworks_data.cancel_type = PTHREAD_CANCEL_DEFERRED;
    list_init_head(&task->uworks_data.def_uactions);

    strcpy(task->short_name,"<N/A>");
  }
  return task;
}

void __free_task_ipc(task_t *task)
{
  if ( !is_thread(task) ) {
    release_task_ipc(task->ipc);
  }
  release_task_ipc_priv(task->ipc_priv);
}

static int __setup_task_ipc(task_t *task,task_t *parent,ulong_t flags,
                            task_creation_attrs_t *attrs)
{
  int r;

  if( flags & CLONE_IPC ) {

    if( !parent->ipc ) {
      return ERR(-EINVAL);
    }

    task->ipc = parent->ipc;
    r = setup_task_ipc(task);
    if( !r ) {
      get_task_ipc(parent);
    }

  } else {

    if( flags & CLONE_REPL_IPC ) {
      r = replicate_ipc(parent->ipc,task);
    } else {
      r = setup_task_ipc(task);
    }

  }
  return ERR(r);
}

static int __setup_task_events(task_t *task,task_t *parent,ulong_t flags,
                               task_privelege_t priv)
{
  if( !(flags & CLONE_MM) || priv == TPL_KERNEL ) {
    task->task_events = memalloc(sizeof(task_events_t));
    if( !task->task_events ) {
      return ERR(-ENOMEM);
    }
    list_init_head(&task->task_events->my_events);
    list_init_head(&task->task_events->listeners);
    atomic_set(&task->task_events->refcount,1);
    mutex_initialize(&task->task_events->lock);
  } else {
    task->task_events=parent->task_events;
    atomic_inc(&task->task_events->refcount);
  }
  return 0;
}

static void __free_task_events(task_t * task)
{
  /* We don't need to free task->task_events
     if task is thread, because task->task_events
     is just a pointer to parent->task_events in this case */
  if ((!is_thread(task)) && (task->task_events))
    memfree(task->task_events);
}

static int __setup_task_sync_data(task_t *task,task_t *parent,ulong_t flags,
                                  task_privelege_t priv)
{
  if( flags & CLONE_MM ) {
    if( !parent->sync_data && (priv != TPL_KERNEL) ) {
      return ERR(-EINVAL);
    }
    if( parent->sync_data ) {
      task->sync_data=parent->sync_data;
      return dup_task_sync_data(parent->sync_data);
    }
  } else if( flags & CLONE_REPL_SYNC ) {
    task->sync_data=replicate_task_sync_data(parent);
    return task->sync_data ? 0 : ERR(-ENOMEM);
  }
  task->sync_data=allocate_task_sync_data();
  return task->sync_data ? 0 : ERR(-ENOMEM);
}

static int __setup_task_security(task_t *task,task_t *parent,ulong_t flags,
                                 task_privelege_t priv)
{
  if( !parent->sobject ) {
    return ERR(-EINVAL);
  }

  if( flags & CLONE_MM ) {
    if( priv == TPL_KERNEL ) {
      task->sobject = s_alloc_task_object(S_KTHREAD_MAC_LABEL,
                                          S_KTHREAD_UID,S_KTHREAD_GID);
    } else {
      s_get_task_object(parent);
      task->sobject=parent->sobject;
    }
  } else {
    task->sobject = s_clone_task_object(parent);
  }
  return task->sobject ? 0 : ERR(-ENOMEM);
}

void __free_signals(task_t *task)
{
  if ( (task) && (task->siginfo.handlers) ){
    task->siginfo.blocked = 0;
    task->siginfo.ignored = DEFAULT_IGNORED_SIGNALS;
    task->siginfo.pending = 0;
    if (atomic_dec_and_test(&task->siginfo.handlers->use_count) ){
      free_signal_handlers(task->siginfo.handlers);
    }
  }
}

static int __setup_signals(task_t *task,task_t *parent,ulong_t flags)
{
  sighandlers_t *shandlers=NULL;
  sigset_t blocked=0,ignored=DEFAULT_IGNORED_SIGNALS;

  if( flags & CLONE_SIGINFO ) {
    if( !parent->siginfo.handlers ) {
      return ERR(-EINVAL);
    }
    shandlers=parent->siginfo.handlers;
    atomic_inc(&shandlers->use_count);

    blocked=parent->siginfo.blocked;
    ignored=parent->siginfo.ignored;
  }

  if( !shandlers ) {
    shandlers=allocate_signal_handlers();
    if( !shandlers ) {
      return ERR(-ENOMEM);
    }
  }

  task->siginfo.blocked=blocked;
  task->siginfo.ignored=ignored;
  task->siginfo.pending=0;
  task->siginfo.handlers=shandlers;
  spinlock_initialize(&task->siginfo.lock, "Siginfo");
  sigqueue_initialize(&task->siginfo.sigqueue,
                      &task->siginfo.pending);

  return 0;
}


static void __free_posix(task_t *task)
{
  if ( (!is_thread(task)) && (task->posix_stuff) )
    release_task_posix_stuff(task->posix_stuff);
}

static long __setup_posix(task_t *task,task_t *parent,
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
    return ERR(-ENOMEM);
  }
}

static void __free_mm(task_t *task)
{
  if (task)
    if(task->task_mm)
      vmm_destroy(task->task_mm);
}

static int __initialize_task_mm(task_t *orig, task_t *target, task_creation_flags_t flags,
                                task_privelege_t priv, task_creation_attrs_t *attrs)
{
  int ret = 0;

  if (!orig || priv == TPL_KERNEL) {
    /* FIXME DK: This arch-dependent code must be removed out from here */
    rpd_t *rpd = task_get_rpd(target);
    rpd->root_dir = KERNEL_ROOT_PDIR()->root_dir;
  }
  else if (flags & CLONE_MM) {
    target->task_mm = orig->task_mm;
  }
  else {
    target->task_mm = vmm_create(target);
    if (!target->task_mm)
      ret = -ENOMEM;
    else {
      map_kernel_area(target->task_mm); /* FIXME DK: remove after debugging */
      ret = vm_mandmaps_roll(target->task_mm);
      if (ret) {
        memfree(target->task_mm);
        goto out;
      }
      if (orig->priv != TPL_KERNEL) {
        ret = vmm_clone(target->task_mm, orig->task_mm, (flags >> TASK_MMCLONE_SHIFT) & VMM_CLONE_MASK);
        if (ret) {
          memfree(target->task_mm);
          goto out;
        }
        if (attrs) {
          if (!attrs->exec_attrs.stack) {
            attrs->exec_attrs.stack = orig->ustack;
            target->ustack = orig->ustack;
          }
          if (!attrs->exec_attrs.per_task_data) {
            attrs->exec_attrs.per_task_data = orig->ptd;
            target->ptd = orig->ptd;
          }
        }
      }
    }
  }

  out:
  return ERR(ret);
}


int create_new_task(task_t *parent,ulong_t flags,task_privelege_t priv, task_t **t,
                    task_creation_attrs_t *attrs)
{
  task_t *task;
  int r = -ENOMEM;
  page_frame_t *stack_pages;
  task_limits_t *limits = NULL;
  struct ns_id_attrs *ns_attrs = NULL;
  struct namespace *ns = NULL;

  if ((flags && !parent) ||
      ((flags & CLONE_MM) && (flags & (CLONE_COW | CLONE_POPULATE))) ||
      ((flags & (CLONE_COW | CLONE_POPULATE)) == (CLONE_COW | CLONE_POPULATE))) {
    return ERR(-EINVAL);
  }

  /* If the task we're going to create is a thread,
     we must ensure that thread-limit is not reached yet */
  if ( (flags & CLONE_MM) && (priv != TPL_KERNEL) &&
       (parent->group_leader->tg_priv->num_threads >= get_limit(parent->limits, LIMIT_TRHEADS) ))
    return ERR(-ENOMEM);
  /* Namespace stuff */
  /* If namespace support is not configured,
     we have only one namespace for all the tasks */

#ifndef CONFIG_ENABLE_NS
  ns = get_root_namespace();
#else
  if(parent->pid == 0) ns = get_root_namespace();
  else ns = parent->namespace->ns;
#endif

  /* Check if have free PID in the namespace */
  if ( !(flags & CLONE_MM) && (priv != TPL_KERNEL) )
  {
    if ( ns->pid_count >= ns->ns_pid_limit )
    {
      return ERR(-ENOMEM);
    }
  }

  /* TODO: [mt] Add memory limit check. */
  /* goto task_create_fault; */
  task = __allocate_task_struct(flags,priv);
  if( !task ) {
    r = -ENOMEM;
    goto task_create_fault;
  }

  ns_attrs = alloc_ns_attrs(ns);
  if(!ns_attrs) goto free_task;
  else task->namespace = ns_attrs;

  r=__alloc_pid_and_tid(task, parent, flags, priv);
  if( r ) {
    goto free_ns_attr;
  }

  /* Create kernel stack for the new task. */
  r = allocate_kernel_stack(&task->kernel_stack);
  if( r != 0 ) {
    goto free_pid;
  }

  r = __initialize_task_mm(parent, task, flags, priv, attrs);
  if( r != 0 ) {
    goto free_stack;
  }

  if( !(stack_pages = alloc_pages(KERNEL_STACK_PAGES,MMPOOL_KERN | AF_CONTIG)) ) {
    r = -ENOMEM;
    goto free_mm;
  }

  r = mmap_kern(task->kernel_stack.low_address, pframe_number(stack_pages), KERNEL_STACK_PAGES,
                KMAP_READ | KMAP_WRITE | KMAP_KERN);
  if( r != 0 ) {
    goto free_stack_pages;
  }

  r=-ENOMEM;


  /* Setup limits. */
  if( !(flags & CLONE_MM) || priv == TPL_KERNEL ) {
    limits = allocate_task_limits();
    if(limits == NULL) {
      goto free_stack_pages;
    } else {
      /* we must inherit default limits from the namespace
        if namespace support is on */
#ifndef CONFIG_ENABLE_NS
      task->limits = get_task_limits(ns->def_limits);
#else
      set_default_task_limits(task->limits);
#endif
    }
  } else task->limits = parent->group_leader->limits;


  r=__setup_task_ipc(task,parent,flags,attrs);
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

  r=__setup_task_events(task,parent,flags,priv);
  if( r ) {
    goto free_posix;
  }

  r=__setup_task_security(task,parent,flags,priv);
  if( r ) {
    goto free_events;
  }

  /* Setup task's initial state. */
  task->state = TASK_STATE_JUST_BORN;
  task->cpu = cpu_id();

  /* Setup scheduler-related stuff. */
  task->scheduler = NULL;
  task->sched_data = NULL;
  task->flags = 0;
  task->priv = priv;

  if( parent->short_name[0] ) {
    strcpy(task->short_name,parent->short_name);
  }

  if( !(r=__add_to_parent(task,parent,flags,priv)) ) {
    *t = task;
    return 0;
  }

 free_events:
  __free_task_events(task);

 free_posix:
  __free_posix(task);

 free_signals:
  __free_signals(task);

 free_uevents:
  free_task_uspace_events_data(task->uspace_events);

 free_sync_data:
  release_task_sync_data(task->sync_data);

 free_ipc:
  __free_task_ipc(task);

 free_limits:
  if(limits) destroy_task_limits(limits);

 free_stack_pages:
  free_pages(stack_pages, KERNEL_STACK_PAGES);

 free_mm:
  __free_mm(task);

 free_stack:
  free_kernel_stack(task->kernel_stack.id);

 free_pid:
  __free_pid_and_tid(task);

 free_ns_attr:
  if(ns_attrs) destroy_ns_attrs(ns_attrs);

 free_task:
  free_pages_addr(task, 1);

 task_create_fault:
  *t = NULL;
  return ERR(r);
}


void destroy_task_struct(struct __task_struct *task)
{
  __free_task_events(task);
  __free_posix(task);
  __free_signals(task);
  free_task_uspace_events_data(task->uspace_events);
  release_task_sync_data(task->sync_data);
  __free_task_ipc(task);
  if(task->namespace) destroy_ns_attrs(task->namespace);
  if(task->limits) destroy_task_limits(task->limits);

  free_pages(virt_to_pframe((void*)task->kernel_stack.low_address), KERNEL_STACK_PAGES);
  __free_mm(task);
  free_kernel_stack(task->kernel_stack.id);
  __free_pid_and_tid(task);
  free_pages_addr(task, 1);
}

void release_task_struct(struct __task_struct *t)
{
  if( atomic_dec_and_test(&t->refcount) ) {
    if( is_thread(t) ) {
      LOCK_TASK_STRUCT(t->group_leader);
      idx_free(&t->group_leader->tg_priv->tid_allocator,t->tid);
      UNLOCK_TASK_STRUCT(t->group_leader);
    } else {
      __free_pid(t->namespace->ns, t->pid);
      destroy_task_limits(t->limits);
    }
    /* namespace attrs */
    destroy_ns_attrs(t->namespace);
    free_pages_addr(t,1);
  }
}
