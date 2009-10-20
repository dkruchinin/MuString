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
 * (c) Copyright 2008 Tirra <tirra.newly@gmail.com>
 * (c) Copyright 2008 Michael Tsymbalyuk <mtzaurus@gmail.com>
 * (c) Copyright 2008 Dan Kruchinin <dan.kruchinin@gmail.com>
 *
 * include/sync/spinlock.h: spinlock and atomic types and functions
 *                         architecture independed
 *
 */

#ifndef __MSTRING_SPINLOCK_H__ /* there are several spinlock.h with different targets */
#define __MSTRING_SPINLOCK_H__

#include <config.h>
#include <arch/atomic.h>
#include <arch/preempt.h>
#include <arch/interrupt.h>
#include <sync/spinlock_types.h>
#include <mstring/assert.h>
#include <mstring/types.h>

#ifdef CONFIG_SMP
#include <arch/spinlock.h>
#include <sync/spinlock_smp.h>
#else /* CONFIG_SMP */
#include <sync/spinlock_up.h>
#endif /* !CONFIG_SMP */

#define SPINLOCK_DEFINE(s, name)                                \
  spinlock_t (s) = SPINLOCK_INITIALIZE(__SPINLOCK_UNLOCKED_V, name)

#define RW_SPINLOCK_DEFINE(s, name)                                 \
  rw_spinlock_t (s) = RW_SPINLOCK_INITIALIZE(__SPINLOCK_UNLOCKED_V, name)

#define bound_spinlock_initialize(bl, bcpu)             \
  do {                                                  \
    (bl)->bound_lock.__cpu = (bcpu);                    \
    (bl)->bound_lock.__lock = __SPINLOCK_UNLOCKED_V;    \
  } while (0)

#ifndef CONFIG_DEBUG_SPINLOCKS

#define SPINLOCK_INITIALIZE(state, name)        \
  { .spin = { .__spin_val = (state) }, }

#define RW_SPINLOCK_INITIALIZE(state, name)     \
  { .rwlock = { .__r = (state), .__w = (state), } }

#define spinlock_initialize(x, name)            \
  ((x)->spin.__spin_val = __SPINLOCK_UNLOCKED_V)

#define rw_spinlock_initialize(x, name)        \
  do {                                          \
    ((x)->rwlock.__r=__SPINLOCK_UNLOCKED_V);    \
    ((x)->rwlock.__w=__SPINLOCK_UNLOCKED_V);    \
  } while (0)

#else /* !CONFIG_DEBUG_SPINLOCKS */

#define SPINLOCK_INITIALIZE(state, name)            \
  { .__spin_val = (state), .__spin_name = (name), }

#define RW_SPINLOCK_INITIALIZE(state, name)     \
  { .__r = (state), __w = (state), __spin_name = (name), }

#define spinlock_initialize(x, sname)           \
  do {                                          \
    (x)->__spin_val = __SPINLOCK_UNLOCKED_V;    \
    (x)->__spin_name = (sname);                 \
  } while (0)

#define rw_spinlock_initialize(x, sname)        \
  do {                                          \
    (x)->__r = __SPINLOCK_UNLOCKED_V;           \
    (x)->__w = __SPINLOCK_UNLOCKED_V;           \
    (x)->__spin_name = (sname);                 \
  } while (0)

#endif /* CONFIG_DEBUG_SPINLOCKS */

static inline void spinlock_lock(spinlock_t *spin)
{
  preempt_disable();
  __spin_lock(&spin->spin);
}

static inline void spinlock_unlock(spinlock_t *spin)
{
  __spin_unlock(&spin->spin);
  preempt_enable();
}

static inline bool spinlcok_trylock(spinlock_t *spin)
{
  bool ret;

  preempt_disable();
  ret = __spin_trylock(&spin->spin);
  if (!ret) {
    preempt_enable();
  }

  return ret;
}

#define spinlock_lock_irqsave(slock, stat)      \
  do {                                          \
    interrupts_save_and_disable(stat);          \
    __spin_lock(&(slock)->spin);                \
  } while (0)

#define spinlock_unlock_irqrestore(slock, stat) \
  do {                                          \
    __spin_unlock(&(slock)->spin);              \
    interrupts_restore(stat);                   \
  } while (0)


static inline void spinlock_lock_read(rw_spinlock_t *rwl)
{
  preempt_disable();
  __rw_lock_read(&rwl->rwlock);
}

static inline void spinlock_lock_write(rw_spinlock_t *rwl)
{
  preempt_disable();
  __rw_lock_write(&rwl->rwlock);
}

static inline void spinlock_unlock_read(rw_spinlock_t *rwl)
{
  __rw_unlock_read(&rwl->rwlock);
  preempt_enable();
}

static inline void spinlock_unlock_write(rw_spinlock_t *rwl)
{
  __rw_unlock_write(&rwl->rwlock);
  preempt_enable();
}

static inline void spinlock_lock_bit(void *bitmap, int bit)
{
  preempt_disable();
  __spin_lock_bit(bitmap, bit);
}

static inline void spinlock_unlock_bit(void *bitmap, int bit)
{
  __spin_unlock_bit(bitmap, bit);
  preempt_enable();
}

static inline void bound_spinlock_lock_cpu(bound_spinlock_t *bsl,
                                           cpu_id_t cpu)
{
  preempt_disable();
  __bound_spin_lock(&bsl->bound_lock, cpu);
}

static inline void bound_spinlock_unlock(bound_spinlock_t *bsl)
{
  __bound_spin_unlock(&bsl->bound_lock);
  preempt_enable();
}

static inline bool bound_spinlock_trylock_cpu(bound_spinlock_t *bsl,
                                              cpu_id_t cpu)
{
  bool isok;

  preempt_disable();
  isok = __bound_spin_trylock(&bsl->bound_lock, cpu);
  if (!isok) {
    preempt_enable();
  }

  return isok;
}

static inline void spinlocks_lock2( spinlock_t *lock1, spinlock_t *lock2)
{
  ASSERT(lock1 != lock2);

  if( lock1 < lock2 ) {
    spinlock_lock(lock1);
    spinlock_lock(lock2);
  } else {
    spinlock_lock(lock2);
    spinlock_lock(lock1);
  }
}

#endif /* __MSTRING_SPINLOCK_H__ */

