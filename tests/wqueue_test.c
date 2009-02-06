#include <test.h>
#include <eza/waitqueue.h>
#include <kernel/syscalls.h>
#include <eza/task.h>
#include <eza/arch/atomic.h>
#include <eza/arch/types.h>

#define WQ_TEST_ID "Wait queue test"
#define WQ_NPRIORS 16

struct wq_thread_ctx {
  test_framework_t *tf;
  wqueue_task_t *wq_task;
  priority_t priority;
  int idx;
};

static priority_t WQ_TEST_PRIORS[WQ_NPRIORS] = { 10, 22, 31, 22, 44, 11, 23, 9, 22, 13, 14, 15, 13, 13, 10, 22 };
static struct wqt_maint_struct {  
  struct wq_thread_ctx tts[WQ_NPRIORS];
  wqueue_t wq;
  atomic_t sleepers;
  bool finished;
} wqt_main;

static void wq_test_thread(void *ctx)
{
  struct wq_thread_ctx *tctx = ctx;
  int ret;
  wqueue_task_t wqt;

  ret = do_scheduler_control(current_task(), SYS_SCHED_CTL_SET_PRIORITY, tctx->priority);
  if (ret) {
    tctx->tf->printf("Can't set new priority to task %d. ERRCODE = %d\n",
                     current_task()->pid, ret);
    tctx->tf->abort();
  }

  waitqueue_prepare_task(&wqt, current_task());  
  atomic_inc(&wqt_main.sleepers);
  ret = waitqueue_push_intr(&wqt_main.wq, &wqt);
  if (ret) {
    tctx->tf->printf("Can't push thread %d\n");
    tctx->tf->abort();
  }
  
  atomic_dec(&wqt_main.sleepers);
}

static void wq_tests_runner(void *ctx)
{
  int i, j;
  int ret;
  test_framework_t *tf = ctx;
  
  for (i = 0; i < WQ_NPRIORS; i++) {
    wqt_main.tts[i].tf = tf;
    wqt_main.tts[i].priority = WQ_TEST_PRIORS[i];
    wqt_main.tts[i].idx = i;
    if (kernel_thread(wq_test_thread, wqt_main.tts + i, NULL)) {
      tf->printf("Can't create kernel thread!");
      tf->abort();
    }
  }
  while (atomic_get(&wqt_main.sleepers) != WQ_NPRIORS);
  tf->printf("All %d thread are sitting in wait queue.\n", wqt_main.sleepers);
  waitqueue_dump(&wqt_main.wq);
  /* sort priorities */
  for (i = 0; i < WQ_NPRIORS; i++) {
    for (j = i + 1; j < WQ_NPRIORS; j++) {
      if (WQ_TEST_PRIORS[j] < WQ_TEST_PRIORS[i]) {
        priority_t tmp = WQ_TEST_PRIORS[j];

        WQ_TEST_PRIORS[j] = WQ_TEST_PRIORS[i];
        WQ_TEST_PRIORS[i] = tmp;
      }
    }
  }

  tf->printf("Ok, now I'm going to wake up them all...\n");
  i = 0;
  while (!waitqueue_is_empty(&wqt_main.wq)) {
    task_t *task;

    ret = waitqueue_pop(&wqt_main.wq, &task);
    if (ret) {
      tf->printf("waitqueue_pop error: %d\n", ret);
      tf->abort();
    }
    if (task->static_priority != WQ_TEST_PRIORS[i++]) {
      tf->printf("Popped task with priority %d, but %d was expected!\n",
                 task->static_priority, WQ_TEST_PRIORS[i - 1]);
      kprintf("PRIORS: [ ");
      for (i = 0; i < WQ_NPRIORS; i++)
        kprintf("%d, ", WQ_TEST_PRIORS[i]);
      kprintf("]\n");
      waitqueue_dump(&wqt_main.wq);
      tf->abort();
    }
  }
  
  while (atomic_get(&wqt_main.sleepers));
  tf->printf("Ok, all guys are waked up now.\n");
  waitqueue_dump(&wqt_main.wq);
  wqt_main.finished = true;
  return;
}

static void wq_test_run(test_framework_t *tf, void *ctx)
{
  if (kernel_thread(wq_tests_runner, tf, NULL)) {
    tf->printf("Can't create kernel thread!");
    tf->abort();
  }

  tf->test_completion_loop(WQ_TEST_ID, &wqt_main.finished);
}

static bool wq_test_initialize(void **ctx)
{
  waitqueue_initialize(&wqt_main.wq);
  wqt_main.finished = false;
  atomic_set(&wqt_main.sleepers, 0);
  
  return true;
}

static void wq_test_deinitialize(void *unused)
{
}

testcase_t wq_testcase = {
  .id = WQ_TEST_ID,
  .initialize = wq_test_initialize,
  .deinitialize = wq_test_deinitialize,
  .run = wq_test_run,  
};

