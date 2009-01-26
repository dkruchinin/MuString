#include <config.h>
#include <mlibc/kprintf.h>
#include <eza/arch/types.h>
#include <test.h>
#include <ds/list.h>
#include <mm/slab.h>

extern testcase_t ipc_testcase;
extern testcase_t sched_testcase;
extern testcase_t wq_testcase;
extern testcase_t usync_testcase;
extern testcase_t signals_testcase;
extern testcase_t tlsf_testcase;
extern testcase_t mapunmap_tc;
extern testcase_t vma_testcase;

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
#ifdef CONFIG_TEST_USYNC
  &usync_testcase,
#endif
#ifdef CONFIG_TEST_SIGNALS
  &signals_testcase,
#endif
#ifdef CONFIG_TEST_TLSF
  &tlsf_testcase,
#endif
#ifdef CONFIG_TEST_MAPUNMAP
  &mapunmap_tc,
#endif
#ifdef CONFIG_TEST_VMA
  &vma_testcase,
#endif /* CONFIG_TEST_VMA */
  NULL,
};

void add_testcases(test_collection_t *tc,void *ctx)
{
  int i;

  for(i=0;known_testcases[i];i++) {
    tc->add_testcase(ctx,known_testcases[i]);
  }
}
