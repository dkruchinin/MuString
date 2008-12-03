#ifndef __BSPINLOCK_H__
#define  __BSPINLOCK_H__

#include <eza/arch/types.h>
#include <eza/raw_sync.h>
#include <eza/arch/spinlock.h>
#include <eza/arch/scheduler.h>
#include <eza/arch/bspinlock.h>

#define binded_spinlock_initialize(b,cpu)       \
  do {                                          \
    (b)->__lock=__SPIN_LOCK_UNLOCKED;           \
    (b)->__cpu=cpu;                             \
  } while(0)

#define binded_spinlock_lock_cpu(b,cpu)         \
  arch_binded_spinlock_lock_cpu(b,cpu)

#define binded_spinlock_unlock_cpu(b,cpu)       \
  arch_binded_spinlock_unlock_cpu(b,cpu)

#define binded_spinlock_lock(b)                 \
  binded_spinlock_lock_cpu(b,cpu_id())

#define binded_spinlock_unlock(b)               \
  binded_spinlock_unlock_cpu(b,cpu_id())

#define binded_spinlock_trylock_cpu(b,cpu)      \
  arch_binded_spinlock_trylock_cpu(b,cpu)

#define binded_spinlock_trylock(b)              \
  binded_spinlock_trylock_cpu(b,cpu_id())

#endif
