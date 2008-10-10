#ifndef __SEMAPHORE__
#define __SEMAPHORE__

#include <eza/arch/types.h>

typedef struct __semaphore {
  volatile ulong_t __sem_val;
} semaphore_t;

typedef struct __rw_semaphore {
  volatile ulong_t __sem_val;
} rw_semaphore_t;

#define DEFINE_SEMAPHORE(n,c)  semaphore_t n = { .__sem_val=c };

#define DEFINE_RW_SEMAPHORE(n,c)  rw_semaphore_t n = { .__sem_val=c };

#define DEFINE_MUTEX(n) semaphore_t n = { .__sem_val=1 };

static inline void semaphore_initialize(semaphore_t *s,ulong_t c)
{
  s->__sem_val = c;
}

static inline void rw_semaphore_initialize(rw_semaphore_t *s,ulong_t c)
{
  s->__sem_val = c;
}


#define semaphore_down(s)
#define semaphore_up(s)

#define rw_semaphore_down(s)
#define rw_semaphore_up(s)

#endif
