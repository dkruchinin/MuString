#ifndef __ATOMIC_H__
#define __ATOMIC_H__

#include <eza/arch/atomic.h>
#ifndef ARCH_HAS_ATOMIC
#include <eza/smp.h>
#include <eza/spinlock.h>
#include <eza/arch/types.h>

typedef struct __atomic {
  volatile long c;
} atomic_t;

/* atomic related functions prototypes
 * used in implementation
 */
static inline void atomic_set(atomic_t *v,long c);
static inline long atomic_get(atomic_t *v);

static inline void atomic_set(atomic_t *v,long c)
{
  v->c=c;

  return;
}

static inline long atomic_get(atomic_t *v)
{
  return v->c;
}

#endif /* ARCH_HAS_ATOMIC */

#endif /* __ATOMIC_H__ */
