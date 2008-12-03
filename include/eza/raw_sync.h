#ifndef __RAW_SYNC_H__
#define  __RAW_SYNC_H__

#include <eza/arch/types.h>

#define __SPIN_LOCK_UNLOCKED  0

/* Binded spinlock. */
typedef struct __binded_spinlock_t {
  ulong_t __lock,__cpu;
} binded_spinlock_t;

#endif
