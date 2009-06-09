#ifndef __RAW_SYNC_H__
#define  __RAW_SYNC_H__

#include <arch/types.h>

#define __SPIN_LOCK_UNLOCKED  0

/* Binded spinlock. */
typedef struct __bound_spinlock_t {
  ulong_t __lock,__cpu;
} bound_spinlock_t;

#endif
