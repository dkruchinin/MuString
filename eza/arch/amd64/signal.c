#include <eza/arch/types.h>
#include <mlibc/kprintf.h>
#include <eza/smp.h>
#include <eza/task.h>

void handle_uworks(int reason, uint64_t retcode)
{
  kprintf("[UWORKS]: %d/%d. Processing works for %d:%d\n",
          reason,retcode,
          current_task()->pid,current_task()->tid);
  kprintf("[UWORKS]: Pending signals: 0x%X\n\n",
          current_task()->siginfo.pending);

  clear_task_signals_pending(current_task());
}
