#ifndef __MSTRING_SPINLOCK_TYPES_H__
#define __MSTRING_SPINLOCK_TYPES_H__

#include <mstring/types.h>

#define __SPINLOCK_LOCKED_V   1
#define __SPINLOCK_UNLOCKED_V 0

struct raw_spinlock {
  lock_t __spin_val;
};

struct raw_rwlock {
  lock_t __r, __w;
};

struct raw_boundlock {
  ulong_t __lock,__cpu;
};

typedef struct __spinlock_type {
  struct raw_spinlock spin;
} spinlock_t;

typedef struct __rw_spinlock_type {
  struct raw_rwlock rwlock;
} rw_spinlock_t;

typedef struct __bound_spinlock_t {
  struct raw_boundlock bound_lock;
} bound_spinlock_t;

#endif
