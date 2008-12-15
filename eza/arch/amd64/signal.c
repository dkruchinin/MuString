#include <eza/arch/types.h>
#include <mlibc/kprintf.h>
#include <eza/smp.h>
#include <eza/task.h>

void handle_uworks(void)
{
  kprintf( "[UWORKS]: Processing works for %d:%d\n",
           current_task()->pid,current_task()->tid);
  clear_task_signals_pending(current_task());
}
