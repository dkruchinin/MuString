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
 * eza/generic_api/process.c: base system process-related functions.
 */

#include <eza/task.h>
#include <mm/mmap.h>
#include <eza/smp.h>
#include <eza/kstack.h>
#include <eza/errno.h>
#include <eza/amd64/context.h>
#include <mlibc/kprintf.h>
#include <eza/arch/scheduler.h>
#include <eza/arch/types.h>
#include <eza/kernel.h>
#include <eza/arch/task.h>
#include <eza/arch/preempt.h>
#include <eza/process.h>
#include <eza/security.h>
#include <eza/arch/current.h>
#include <eza/spinlock.h>
#include <kernel/syscalls.h>
#include <kernel/vm.h>

typedef uint32_t hash_level_t;

/* Stuff related to PID-to-task translation. */
static rw_spinlock_t pid_to_struct_locks[PID_HASH_LEVELS];
static list_head_t pid_to_struct_hash[PID_HASH_LEVELS];

/* Macros for dealing with PID-to-task hash locks.
 * _W means 'for writing', '_R' means 'for reading' */
#define LOCK_PID_HASH_LEVEL_R(l) spinlock_lock_read(&pid_to_struct_locks[l])
#define UNLOCK_PID_HASH_LEVEL_R(l) spinlock_unlock_read(&pid_to_struct_locks[l])
#define LOCK_PID_HASH_LEVEL_W(l) spinlock_lock_write(&pid_to_struct_locks[l])
#define UNLOCK_PID_HASH_LEVEL_W(l) spinlock_unlock_write(&pid_to_struct_locks[l])

void initialize_process_subsystem(void)
{
  uint32_t i;

  /* Now initialize PID-hash arrays. */
  for( i=0; i<PID_HASH_LEVELS; i++ ) {
    rw_spinlock_initialize(&pid_to_struct_locks[i], "PID-to-task lock");
    list_init_head(&pid_to_struct_hash[i]);
  }
}

static hash_level_t pid_to_hash_level(pid_t pid)
{
  return (pid & PID_HASH_LEVEL_MASK);
}

task_t *pid_to_task(pid_t pid)
{
  if( pid < NUM_PIDS ) {
    hash_level_t l = pid_to_hash_level(pid);
    list_node_t *n;

    LOCK_PID_HASH_LEVEL_R(l);
    list_for_each(&pid_to_struct_hash[l],n) {
      task_t *t = container_of(n,task_t,pid_list);
      if(t->pid == pid) {
        grab_task_struct(t);
        UNLOCK_PID_HASH_LEVEL_R(l);
        return t;
      }
    }
    UNLOCK_PID_HASH_LEVEL_R(l);
  }

  return NULL;
}

status_t create_task(task_t *parent,task_creation_flags_t flags,task_privelege_t priv,
                     task_t **newtask)
{
  task_t *new_task;
  status_t r;

  r = create_new_task(parent,flags,priv,&new_task);
  if(r == 0) {
    r = arch_setup_task_context(new_task,flags,priv);
    if(r == 0) {
      /* Tell the scheduler layer to take care of this task. */
      r = sched_add_task(new_task);
      if( r == 0 ) {
        /* Now we can add this task to the hash. */
        hash_level_t l = pid_to_hash_level(new_task->pid);
        LOCK_PID_HASH_LEVEL_W(l);
        list_add2tail(&pid_to_struct_hash[l],&new_task->pid_list);
        UNLOCK_PID_HASH_LEVEL_W(l);
      }
    } else {
      /* TODO: [mt] deallocate task struct properly. */
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

status_t do_task_control(task_t *target,ulong_t cmd, ulong_t arg)
{
  switch( cmd ) {
    case SYS_PR_CTL_SET_ENTRYPOINT:
    case SYS_PR_CTL_SET_STACK:
      if( !valid_user_address(arg) ) {
        return -EFAULT;
      }
      if( target->state == TASK_STATE_JUST_BORN ) {
        return arch_process_context_control(target,cmd,arg);
      }
      break;
    case SYS_PR_CTL_GET_ENTRYPOINT:
    case SYS_PR_CTL_GET_STACK:
      /* No arguments are acceptable for these commands. */
      if( arg == 0 && target->state == TASK_STATE_JUST_BORN ) {
        return arch_process_context_control(target,cmd,arg);
      }
  }
  return -EINVAL;
}

status_t sys_task_control(pid_t pid, ulong_t cmd, ulong_t arg)
{
  status_t r;
  task_t *task = pid_to_task(pid);

  if( task == NULL ) {
    return -ESRCH;
  }

  if( !security_ops->check_process_control(task,cmd,arg) ) {
    r = -EACCES;
    goto out_release;
  }

  r = do_task_control(task,cmd,arg);
out_release:
  release_task_struct(task);
  return r;
}

status_t sys_create_task(task_creation_flags_t flags)
{
  task_t *task;
  status_t r;
  
  if( !security_ops->check_create_process(flags) ) {
    return -EPERM;
  }

  r = create_task(current_task(), flags, TPL_USER, &task);
  if( r == 0 ) {
    r = task->pid;
  }
  return r;
}

extern ulong_t syscall_counter;

status_t sys_get_pid(void)
{
//  kprintf( "sys_get_pid(): Process %d (CPU #%d) wants to know its PID.\n",
//           current_task()->pid, cpu_id() );
  syscall_counter++;
  return current_task()->pid;
}
