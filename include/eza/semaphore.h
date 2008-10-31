#ifndef __SEMAPHORE__
#define __SEMAPHORE__

#include <ds/list.h>
#include <eza/spinlock.h>
#include <eza/arch/types.h>

typedef struct __semaphore {
  int var;
  spinlock_t lock;
  list_head_t wait_queue;
} semaphore_t;

#define SEMAPHORE_DEFINE(name, initval)                 \
  semaphore_t (name) = {                                \
    .var = (initval);                                   \
    .lock = SPINLOCK_INITIALIZE(__SPINLOCK_UNLOCKED_V); \
    .wait_queue = LIST_INITIALIZE((name).wait_queue);   \
  }

void sem_initialize(semaphore_t *sem);

#endif
