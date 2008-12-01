#include <mlibc/kprintf.h>
#include <eza/arch/types.h>
#include <test.h>
#include <ds/list.h>
#include <mm/slab.h>

typedef struct __tc_ctx {
  list_head_t testcases;
  int num_testcases,num_executed;
} tc_ctx_t;

static bool __def_initialize(void **ctx)
{
  tc_ctx_t *tc_ctx=memalloc(sizeof(*tc_ctx));

  if( tc_ctx ) {
    list_init_head(&tc_ctx->testcases);
    tc_ctx->num_testcases=0;
    *ctx=tc_ctx;
    return true;
  }
  return false;
}

static void __def_add_testcase(void *ctx,testcase_t *tcase)
{
  tc_ctx_t *tc_ctx=(tc_ctx_t *)ctx;
  list_add2tail(&tc_ctx->testcases,&tcase->l);
  tc_ctx->num_testcases++;
}

static void __def_run(test_framework_t *tf,void *ctx)
{
  tc_ctx_t *tc_ctx=(tc_ctx_t *)ctx;
  list_node_t *n;

  list_for_each(&tc_ctx->testcases,n) {
    testcase_t *tcase=container_of(n,testcase_t,l);
    void *t_ctx;

    if( !tcase->initialize(&t_ctx) ) {
      tf->printf( "** Failed to initialize testcase: '%s' !\n",
                  tcase->id );
      break;
    }

    tf->printf( "Running testcase: '%s'\n", tcase->id );
    tcase->run(tf,t_ctx);
    tcase->deinitialize(t_ctx);

    tc_ctx->num_executed++;
    if( !tf->continue_testing() ) {
      break;
    }
  }
}

static void __def_deinitialize(void *ctx)
{
  memfree(ctx);
}

static int __def_get_num_testcases(void *ctx)
{
  tc_ctx_t *tc_ctx=(tc_ctx_t *)ctx;
  return tc_ctx->num_testcases;
}

static int __def_get_num_executed(void *ctx)
{
  tc_ctx_t *tc_ctx=(tc_ctx_t *)ctx;
  return tc_ctx->num_executed;
}

static test_collection_t __def_test_collection = {
  .initialize=__def_initialize,
  .add_testcase=__def_add_testcase,
  .run=__def_run,
  .deinitialize=__def_deinitialize,
  .get_num_testcases=__def_get_num_testcases,
  .get_num_executed=__def_get_num_executed,
};

test_collection_t *create_test_collection(void **ctx)
{
  return &__def_test_collection;
}
