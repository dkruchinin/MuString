#ifndef __SEMAPHORE__
#define __SEMAPHORE__

#include <config.h>
#include <ds/list.h>
#include <eza/spinlock.h>
#include <eza/smp.h>
#include <eza/arch/types.h>

typedef struct __semaphore {
  int accs;
  spinlock_t lock;
    //wait_queue_t wait_queue;
} semaphore_t;

#define SEMAPHORE_DEFINE(name, initval)                 \
  semaphore_t (name) = {                                \
    .var = (initval);                                   \
    .lock = SPINLOCK_INITIALIZE(__SPINLOCK_UNLOCKED_V); \
    //.wait_queue = LIST_INITIALIZE((name).wait_queue); \
  }

void sem_initialize(semaphore_t *sem, int initval);
void sem_down(semaphore_t *sem);
void sem_up(semaphore_t *sem);
void sem_down_n(semaphore_t *sem, int n);
void sem_up_n(semaphore_t *sem, int n);

#endif /* __SEMAPHORE__ */
