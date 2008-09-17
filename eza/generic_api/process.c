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
#include <eza/spinlock.h>
#include <eza/arch/preempt.h>

typedef uint32_t hash_level_t;

/* Stuff related to PID-to-task translation. */
static spinlock_t pid_to_struct_locks[PID_HASH_LEVELS];
static list_head_t pid_to_struct_hash[PID_HASH_LEVELS];

/* Macros for dealing with PID-to-task hash locks.
 * _W means 'for writing', '_R' means 'for reading' */
#define LOCK_PID_HASH_LEVEL_R(l) spinlock_lock(&pid_to_struct_locks[l])
#define UNLOCK_PID_HASH_LEVEL_R(l) spinlock_unlock(&pid_to_struct_locks[l])
#define LOCK_PID_HASH_LEVEL_W(l) spinlock_lock(&pid_to_struct_locks[l])
#define UNLOCK_PID_HASH_LEVEL_W(l) spinlock_unlock(&pid_to_struct_locks[l])

void initialize_process_subsystem(void)
{
  uint32_t i;

  /* Now initialize PID-hash arrays. */
  for( i=0; i<PID_HASH_LEVELS; i++ ) {
    spinlock_initialize(&pid_to_struct_locks[i], "PID-to-task lock");
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

  r = create_new_task(parent,&new_task,flags,priv);
  if(r == 0) {
    r = arch_setup_task_context(new_task,flags,priv);
    if(r == 0) {
      hash_level_t l = pid_to_hash_level(new_task->pid);
      LOCK_PID_HASH_LEVEL_W(l);
      list_add2tail(&pid_to_struct_hash[l],&new_task->pid_list);
      UNLOCK_PID_HASH_LEVEL_W(l);

      /* Tell the scheduler layer to take care of this task. */
      sched_add_task(new_task);
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


