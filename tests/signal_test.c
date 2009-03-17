#include <eza/kernel.h>
#include <mlibc/kprintf.h>
#include <eza/smp.h>
#include <eza/arch/scheduler.h>
#include <eza/arch/types.h>
#include <eza/task.h>
#include <eza/scheduler.h>
#include <eza/swks.h>
#include <mlibc/string.h>
#include <eza/arch/preempt.h>
#include <eza/spinlock.h>
#include <ipc/ipc.h>
#include <ipc/port.h>
#include <eza/arch/asm.h>
#include <eza/arch/preempt.h>
#include <kernel/syscalls.h>
#include <eza/uinterrupt.h>
#include <ipc/poll.h>
#include <eza/gc.h>
#include <ipc/channel.h>
#include <test.h>
#include <mm/slab.h>
#include <eza/errno.h>
#include <eza/process.h>

#define TEST_ID  "Signal subsystem test"
#define SERVER_THREAD  "[SERVER THREAD] "
#define CLIENT_THREAD  "[CLIENT THREAD] "

typedef struct __signal_test_ctx {
  test_framework_t *tf;
  ulong_t server_pid;
  bool tests_finished;
} signal_test_ctx_t;

#define DECLARE_TEST_CONTEXT  signal_test_ctx_t *tctx=(signal_test_ctx_t*)ctx; \
  test_framework_t *tf=tctx->tf

static void __thread1(void *ctx) {
  DECLARE_TEST_CONTEXT;
  sys_exit(0);
}

static void __server_thread(void *ctx)
{
  DECLARE_TEST_CONTEXT;
  task_t *t;

  tf->printf("Starting signal tests.\n");
  if( kernel_thread(__thread1,ctx,&t) ) {
    tf->printf("Can't create test thread N 1 !\n");
    tf->abort();
  }

  tf->printf("All signal tests finished.\n");
  tctx->tests_finished=true;
  sys_exit(0);
}

static bool __signal_tests_initialize(void **ctx)
{
  signal_test_ctx_t *tctx=memalloc(sizeof(*tctx));

  if( tctx ) {
    memset(tctx,0,sizeof(*tctx));
    tctx->tests_finished=false;
    *ctx=tctx;
    return true;
  }
  return false;
}

void __signal_tests_run(test_framework_t *f,void *ctx)
{
  signal_test_ctx_t *tctx=(signal_test_ctx_t*)ctx;

  tctx->tf=f;

  if( kernel_thread(__server_thread,tctx,NULL) ) {
    f->printf( "Can't create server thread !" );
    f->abort();
  } else {
    f->test_completion_loop(TEST_ID,&tctx->tests_finished);
  }
}


void __signal_tests_deinitialize(void *ctx)
{
  memfree(ctx);
}

testcase_t signals_testcase={
  .id=TEST_ID,
  .initialize=__signal_tests_initialize,
  .deinitialize=__signal_tests_deinitialize,
  .run=__signal_tests_run,
};

