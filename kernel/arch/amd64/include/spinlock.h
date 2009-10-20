/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * (c) Copyright 2006,2007,2008 MString Core Team <http://mstring.jarios.org>
 * (c) Copyright 2008 Michael Tsymbalyuk <mtzaurus@gmail.com>
 * (c) Copyright 2008 Dan Kruchinin <dan.kruchinin@gmail.com>
 *
 * include/mstring/amd64/spinlock.h: spinlock and atomic amd64 specific and 
 *                               extended functions
 *
 */

#ifndef __AMD64_SPINLOCK_H__
#define __AMD64_SPINLOCK_H__

#include <config.h>
#include <arch/types.h>
#include <arch/bitwise.h>
#include <arch/asm.h>
#include <sync/spinlock_types.h>
#include <mstring/types.h>

#define arch_spinlock_lock_bit(bitmap, bit)     \
  while (arch_bit_test_and_set(bitmap, bit))

#define arch_spinlock_unlock_bit(bitmap, bit)   \
  arch_bit_clear(bitmap, bit)

static always_inline void arch_spinlock_lock(struct raw_spinlock *lock)
{
  __asm__ __volatile__(  "movl %2,%%eax\n"
                         "1:" __LOCK_PREFIX "cmpxchgl %0, %1\n"
                         "cmp %2, %%eax\n"
                         "jnz 1b\n"
                         :: "r"(__SPINLOCK_LOCKED_V),"m"(lock->__spin_val), "rI"(__SPINLOCK_UNLOCKED_V)
                         : "%rax", "memory" );
}

static always_inline void arch_spinlock_unlock(struct raw_spinlock *lock)
{
  __asm__ __volatile__( __LOCK_PREFIX "xchgl %0, %1\n"
                        :: "r"(__SPINLOCK_UNLOCKED_V), "m"( lock->__spin_val )
                        : "memory" );
}

static always_inline bool arch_spinlock_trylock(struct raw_spinlock *lock)
{
  int ret = __SPINLOCK_UNLOCKED_V;
  __asm__ volatile (__LOCK_PREFIX "cmpxchgl %2, %0\n\t"
                    : "+m" (lock->__spin_val), "=&a" (ret)
                    : "Ir" (__SPINLOCK_LOCKED_V));

  return !ret;
}

static always_inline bool arch_spinlock_is_locked(struct raw_spinlock *lock)
{
  return (lock->__spin_val == __SPINLOCK_LOCKED_V);
}

/* Main strategy of JariOS read-write spinlocks.
 * We use two counters: one for counting readers and one for counting
 * writers. There can be only 0 or 1 writers, but unlimited amount of
 * readers, so we can use bits [1 .. N] of the 'writers' counter for
 * our needs. We deal with RW spinlocks in two steps. First, we implement
 * a pure spinlock (to access these two variables atomically) via 'btr'
 * instruction. After we've grabbed the first lock, we compare its value
 * against 256, since we use 8th bit of the 'writers' counter - since
 * there can be only 0 or 1 writers, we will always get either 256 or
 * 257 in this counter after setting 8th bit (after locking the lock).
 * So if we have 257, this means that our RW lock has been grabbed
 * by a writer and we must release the bit and repeat the procedure
 * from the beginning waiting for the writer to decrement the counter.
 */

static always_inline void arch_spinlock_lock_read(struct raw_rwlock *lock)
{
  __asm__ __volatile__( "0: movq %0,%%rax\n"
                        "1:" __LOCK_PREFIX "bts %%rax,%1\n"
                        "jc 1b\n"
                        "cmpl $256, %1\n"
                        "je 3f\n"
                        __LOCK_PREFIX "btr %%rax,%1\n"
                        "jmp 0b\n"
                        "3: " __LOCK_PREFIX "incl %2\n"
                        "4: " __LOCK_PREFIX "btr\r %%rax,%1\n"
                        :: "rI"(8), "m"(lock->__w),
                        "m"(lock->__r) : "%rax", "memory" );
}

static always_inline void arch_spinlock_lock_write(struct raw_rwlock *lock)
{
  __asm__ __volatile__( "0: movq %0,%%rax\n"
                        "1:" __LOCK_PREFIX "bts %%rax,%1\n"
                        "jc 1b\n"
                        "cmpl $256, %1\n"
                        "je 3f\n"
                        "2:" __LOCK_PREFIX "btr %%rax,%1\n"
                        "jmp 0b\n"
                        "3: cmpl $0, %2\n"
                        "jne 2b\n"
                        __LOCK_PREFIX "incl %1\n"
                        __LOCK_PREFIX "btr\r %%rax,%1\n"
                        :: "rI"(8), "m"(lock->__w),
                        "m"(lock->__r) : "%rax", "memory" );  
}

static always_inline void arch_spinlock_unlock_read(struct raw_rwlock *lock)
{
  __asm__ __volatile__( __LOCK_PREFIX "decl %0\n"
                        :: "m"(lock->__r)
                        : "memory" );
}

static always_inline void arch_spinlock_unlock_write(struct raw_rwlock *lock)
{
  __asm__ __volatile__( __LOCK_PREFIX "decl %0\n"
                        :: "m"(lock->__w)
                        : "memory" );
}

/* CPU-bound spinlocks. */
static inline void arch_bound_spinlock_lock_cpu(struct raw_boundlock *l,
                                                ulong_t cpu)
{
  __asm__ __volatile__(
    "cmp %0,%2\n"
    "jne 101f\n"
    /* Owner is accessing the lock. */
    __LOCK_PREFIX "add $1,%1\n"
    "11:" __LOCK_PREFIX "bts $15,%1\n"
    "jc 11b\n"
    /* Lock is successfully granted */
    __LOCK_PREFIX "sub $1,%1\n"
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
    :: "r"((lock_t)cpu),"m"((lock_t)l->__lock),
     "r"((lock_t)l->__cpu),"r"((lock_t)0):
     "memory" );
}

static inline void arch_bound_spinlock_unlock_cpu(struct raw_boundlock *l)
{
   __asm__ __volatile__(
     __LOCK_PREFIX "btr $15,%0\n"
     :: "m"((lock_t)l->__lock)
     :
     "memory" );
}

static inline bool arch_bound_spinlock_trylock_cpu(struct raw_boundlock *l,
                                                   ulong_t cpu)
{
  ulong_t locked;

  __asm__ __volatile__(
    "xor %4,%4\n"
    "cmp %0,%2\n"
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
    :: "r"((lock_t)cpu),"m"((lock_t)l->__lock),"r"((lock_t)l->__cpu),
     "m"(locked),"r"((lock_t)0): "memory" );

  return !locked;
}

#endif /* __AMD64_SPINLOCK_H__ */

