#include <eza/arch/types.h>
#include <eza/task.h>
#include <eza/process.h>
#include <eza/errno.h>
#include <eza/arch/context.h>

status_t sys_kill(pid_t pid,int sig)
{
  task_t *task=pid_to_task(pid);
  arch_context_t *arch_ctx;

  if( !task ) {
    kprintf("sys_kill: %d wasn't located !\n",pid);
    return -ESRCH;
  }

  kprintf("sys_kill: Sending a signal to %d:%d\n",
          task->pid,task->tid);
  set_task_signals_pending(task);

  return 0;
}
