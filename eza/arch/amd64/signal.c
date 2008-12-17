#include <eza/arch/types.h>
#include <mlibc/kprintf.h>
#include <eza/smp.h>
#include <eza/task.h>
#include <eza/arch/context.h>

static status_t __prepare_signal_ctx_syscall(uint64_t retcode, uint64_t stack)
{
  return 0;
}

static status_t __prepare_signal_ctx_int(uint64_t retcode, uint64_t stack)
{
  return 0;
}

void handle_uworks(int reason, uint64_t retcode, uint64_t stack)
{
  status_t r;

  kprintf("[UWORKS]: %d/%d. Processing works for %d:%d\n",
          reason,retcode,
          current_task()->pid,current_task()->tid);
  kprintf("[UWORKS]: Pending signals: 0x%X\n",
          current_task()->siginfo.pending);

  switch( reason ) {
    case __SYCALL_UWORK:
      r=__prepare_signal_ctx_syscall(retcode,stack);
      break;
    default:
      r=__prepare_signal_ctx_int(retcode,stack);
      break;
  }

  if( !r ) {
  }
  clear_task_signals_pending(current_task());
}

status_t sys_sigreturn(void *ctx) {
  return 0;
}
