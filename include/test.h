#ifndef __TEST_H__
#define  __TEST_H__

#include <arch/types.h>
#include <ds/list.h>

typedef struct __test_framework {
  void (*passed)(void);
  void (*failed)(void);
  void (*abort)(void);
  void (*printf)(const char *fmt, ...);
  void (*set_first_fault_hit)(bool set);
  bool (*continue_testing)(void);
  void (*test_completion_loop)(const char *id,bool *flag);
} test_framework_t;

typedef struct __testcase {
  list_node_t l;
  const char *id;
  bool (*initialize)(void **ctx);
  void (*run)(test_framework_t *f,void *ctx);
  void (*deinitialize)(void *ctx);
  bool autodeploy_threads;
} testcase_t;

typedef struct __test_collection {
  bool (*initialize)(void **ctx);
  void (*add_testcase)(void *ctx,testcase_t *tcase);
  void (*run)(test_framework_t *tf,void *ctx);
  void (*deinitialize)(void *ctx);
  int (*get_num_testcases)(void *ctx);
  int (*get_num_executed)(void *ctx);
} test_collection_t;

void run_tests(void);
test_collection_t *create_test_collection(void **ctx);
void add_testcases(test_collection_t *tc,void *ctx);

#endif
