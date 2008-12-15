#include <eza/arch/types.h>
#include <eza/task.h>
#include <eza/process.h>

status_t sys_kill(pid_t pid,int sig)
{
  task_t *task;

  arch_set_task_signals_pending(current_task());

  return 0;
}
