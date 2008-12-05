#include <config.h>
#include <mlibc/kprintf.h>
#include <eza/arch/types.h>
#include <test.h>
#include <ds/list.h>
#include <mm/slab.h>

extern testcase_t ipc_testcase;
extern testcase_t sched_testcase;
extern testcase_t wq_testcase;

static testcase_t *known_testcases[] = {
#ifdef CONFIG_TEST_IPC
  &ipc_testcase,
#endif
#ifdef CONFIG_TEST_SCHEDULER
  &sched_testcase,
#endif
#ifdef CONFIG_TEST_WQ
  &wq_testcase,
#endif
  NULL,
};

void add_testcases(test_collection_t *tc,void *ctx)
{
  int i;

  for(i=0;known_testcases[i];i++) {
    tc->add_testcase(ctx,known_testcases[i]);
  }
}
