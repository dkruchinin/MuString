#ifndef __LIMITS__
#define  __LIMITS__

#include <eza/spinlock.h>
#include <eza/arch/atomic.h>

#define LIMIT_UNLIMITED ~0UL

#define LIMIT_IPC_MAX_PORTS  0
#define LIMIT_IPC_MAX_PORT_MESSAGES  1
#define LIMIT_IPC_MAX_CHANNELS 2

#define LIMIT_NUM_LIMITS 3

typedef struct __task_limits {
  spinlock_t lock;
  atomic_t use_count;
  ulong_t limits[LIMIT_NUM_LIMITS];
} task_limits_t;

task_limits_t *allocate_task_limits(void);
void set_default_task_limits(task_limits_t *l);

static inline void get_task_limits(task_limits_t *l)
{
  atomic_inc(&l->use_count);
}

static inline void release_task_limits(task_limits_t *l)
{
  atomic_dec(&l->use_count);
}


#endif
