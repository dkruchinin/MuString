#include <eza/kernel.h>
#include <mlibc/kprintf.h>
#include <eza/smp.h>
#include <eza/arch/scheduler.h>
#include <eza/arch/types.h>
#include <eza/task.h>
#include <eza/scheduler.h>
#include <eza/swks.h>
#include <mlibc/string.h>
#include <eza/arch/mm_types.h>
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
#include <ipc/gen_port.h>
#include <ipc/channel.h>
#include <test.h>
#include <mm/slab.h>
#include <eza/errno.h>

#define TEST_ID "Scheduler tests"
#define SERVER_ID "[Migration test] "
#define TRAVELLER_ID "[CPU Traveller] "

typedef struct __sched_test_ctx {
  test_framework_t *tf;
  ulong_t server_pid;
  bool tests_finished;
} sched_test_ctx_t;

typedef struct __sched_thread_data {
  ulong_t target_cpu;
  test_framework_t *tf;
} sched_thread_data_t;

static void __traveller_thread(void *d)
{
  sched_thread_data_t *td=(sched_thread_data_t*)d;
  test_framework_t *tf=td->tf;

  tf->printf(TRAVELLER_ID "PID: %d, Starting on CPU %d, Target CPU: %d\n",
             current_task()->pid,cpu_id(),td->target_cpu);

  if( cpu_id() != td->target_cpu ) {
    tf->failed();
  } else {
    tf->passed();
  }

  sys_exit(0);
}

static void __migration_test(void *d)
{
  sched_test_ctx_t *tctx=(sched_test_ctx_t*)d;
  test_framework_t *tf=tctx->tf;
  sched_thread_data_t *thread_data[NR_CPUS-1];
  int i,r;

  if( do_scheduler_control(current_task(),SYS_SCHED_CTL_SET_POLICY,
                           SCHED_FIFO) ) {
    panic(SERVER_ID "Can't change my scheduling policy to SCHED_FIFO !\n" );
  }

  for(i=0;i<NR_CPUS-1;i++) {
    sched_thread_data_t *td=memalloc(sizeof(*td));
    task_t *t;

    if( !td ) {
      tf->printf(SERVER_ID"Can't allocate data buffer !\n" );
      tf->abort();
    }

    td->target_cpu=i+1;
    td->tf=tf;
    thread_data[i]=td;

    if( kernel_thread(__traveller_thread,td,&t ) ) {
      tf->printf( SERVER_ID "Can't create traveller thread !\n" );
      tf->abort();
    }

    r=sched_move_task_to_cpu(t,td->target_cpu);
    if( r ) {
      tf->printf(SERVER_ID "Can't move task %d to CPU %d: r=%d\n",
                 t->pid,td->target_cpu,r);
      tf->failed();
    }
  }

  sleep(HZ/10);

  for(i=0;i<NR_CPUS-1;i++) {
    memfree(thread_data[i]);
  }
}

static void __priority_test(void *d)
{
}

static void __test_thread(void *d)
{
  sched_test_ctx_t *tctx=(sched_test_ctx_t*)d;

  tctx->tf->printf(SERVER_ID "Calling migration tests.\n");
  __migration_test(tctx);
  tctx->tf->printf(SERVER_ID "Calling priority tests.\n");
  __priority_test(tctx);

  tctx->tf->printf(SERVER_ID "All scheduler tests finished.\n");
  tctx->tests_finished=true;
  sys_exit(0);
}

static bool __sched_tests_initialize(void **ctx)
{
  sched_test_ctx_t *tctx=memalloc(sizeof(*tctx));

  if( tctx ) {
    memset(tctx,0,sizeof(*tctx));
    return true;
  }

  return false;
}

void __sched_tests_run(test_framework_t *f,void *ctx)
{
  sched_test_ctx_t *tctx=(sched_test_ctx_t*)ctx;
  tctx->tf=f;

  if( kernel_thread(__test_thread,tctx,NULL) ) {
    f->printf( "Can't create main test thread !" );
    f->abort();
  } else {
    tctx->tests_finished=false;
    f->test_completion_loop(TEST_ID,&tctx->tests_finished);
  }
}

void __sched_tests_deinitialize(void *ctx)
{
  memfree(ctx);
}

testcase_t sched_testcase={
  .id=TEST_ID,
  .initialize=__sched_tests_initialize,
  .deinitialize=__sched_tests_deinitialize,
  .run=__sched_tests_run,
};

