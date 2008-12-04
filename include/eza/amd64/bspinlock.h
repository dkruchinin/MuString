#ifndef __ARCH_BSPINLOCK__
#define __ARCH_BSPINLOCK__

#include <eza/arch/types.h>
#include <eza/raw_sync.h>
#include <eza/arch/asm.h>

static inline void __arch_bound_spinlock_lock_cpu(binded_spinlock_t *l,
                                                  ulong_t cpu)
{
  __asm__ __volatile__(
    "cmpq %0,%2\n"
    "jne 101f\n"
    /* Owner is accessing the lock. */
    __LOCK_PREFIX "incq %1\n"
    "11:" __LOCK_PREFIX "bts $15,%1\n"
    "jc 11b\n"
    /* Lock is successfully granted */
    __LOCK_PREFIX "decq %1\n"
    "jmp 1000f\n"

    /* Not owner is accessing the lock. */
    "101:" __LOCK_PREFIX "bts $15,%1\n"
    "jc 101b\n"
    /* Lock is granted, so check if there are pending owners. */
    __LOCK_PREFIX "add $0,%1\n"
    "mov %1,%3\n"
    "cmp $32768,%3\n"
    "je 1000f\n"
    /* No luck - pending owner wants to access the lock. */
    __LOCK_PREFIX "btr $15,%1\n"
    "jmp 101b\n"

    /* No pending owners - the lock is granted. */
    "1000: \n"
    :: "r"(cpu),"m"(l->__lock),
     "r"(l->__cpu),"r"(0):
     "memory" );
}

static inline void __arch_bound_spinlock_unlock_cpu(binded_spinlock_t *l,
                                                    ulong_t cpu)
{
   __asm__ __volatile__(
     __LOCK_PREFIX "btr $15,%0\n"
     :: "m"(l->__lock)
     :
     "memory" );
}

static inline bool __arch_bound_spinlock_trylock_cpu(binded_spinlock_t *l,
                                                     ulong_t cpu)
{
  ulong_t locked;

  __asm__ __volatile__(
    "xor %4,%4\n"
    "cmpq %0,%2\n"
    "jne 101f\n"
    /* Owner is trying to access the lock.*/
    __LOCK_PREFIX "bts $15,%1\n"
    "adc $0,%4\n"
    "jmp 1000f\n"

    /* Not owner is trying to grab the lock. */
    "101: " __LOCK_PREFIX "bts $15,%1\n"
    "adc $0,%4\n"
    /* No luck - the lock is already locked. */
    "jnz 1000f\n"
    /* Lock is ours, so check for pending owners. */
    __LOCK_PREFIX "add $0,%1\n"
    "mov %1,%4\n"
    "sub $32768,%4\n"
    /* The lock is ours ! */
    "jz 1000f\n"

    /* No luck - pending owners detected. So release the lock. */
    __LOCK_PREFIX "btr $15,%1\n"
    "1000: mov %4,%3\n"
    "\n"
    :: "r"(cpu),"m"(l->__lock),"r"(l->__cpu),
     "m"(locked),"r"(0): "memory" );

  return !locked;
}

#define arch_bound_spinlock_lock_cpu(b,cpu)   \
  __arch_bound_spinlock_lock_cpu(b,cpu)

#define arch_bound_spinlock_unlock_cpu(b,cpu) \
  __arch_bound_spinlock_unlock_cpu(b,cpu)

#define arch_bound_spinlock_trylock_cpu(b,cpu) \
  __arch_bound_spinlock_trylock_cpu(b,cpu)

#endif
