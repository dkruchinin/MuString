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
 * (c) Copyright 2006,2007,2008 MString Core Team <http://mstring.berlios.de>
 * (c) Copyright 2008 Tirra <tirra.newly@gmail.com>
 * (c) Copyright 2008 Michael Tsymbalyuk <mtzaurus@gmail.com>
 * (c) Copyright 2008 Dan Kruchinin <dan.kruchinin@gmail.com>
 *
 * include/eza/spinlock.h: spinlock and atomic types and functions
 *                         architecture independed
 *
 */

#ifndef __SPINLOCK_H__ /* there are several spinlock.h with different targets */
#define __SPINLOCK_H__

#include <config.h>
#include <eza/arch/atomic.h>
#include <eza/arch/types.h>
#include <eza/arch/preempt.h>
#include <eza/arch/interrupt.h>
#include <eza/arch/spinlock.h>
#include <eza/raw_sync.h>

#ifdef CONFIG_SMP
#define SPINLOCK_INITIALIZE(state)              \
    { .__spin_val = (state) }

#define RW_SPINLOCK_INITIALIZE(state)           \
    { .__r = (state), .__w = (state), }

#define SPINLOCK_DEFINE(s)                      \
    spinlock_t (s) = SPINLOCK_INITIALIZE(__SPINLOCK_UNLOCKED_V)

#define RW_SPINLOCK_DEFINE(s)                   \
    rw_spinlock_t (s) = RW_SPINLOCK_INITIALIZE(__SPINLOCK_UNLOCKED_V)

#define spinlock_initialize(x)                              \
  ((x)->__spin_val = __SPINLOCK_UNLOCKED_V)

#define rw_spinlock_initialize(x)                                   \
  do {                                                              \
    ((x)->__r=__SPINLOCK_UNLOCKED_V);                               \
    ((x)->__w=__SPINLOCK_UNLOCKED_V);                               \
  } while (0)

#define __lock_spin(type, s...)                 \
  do {                                          \
    preempt_disable();                          \
    arch_spinlock_##type(s);                    \
  } while (0)

#define __unlock_spin(type, s...)               \
  do {                                          \
    arch_spinlock_##type(s);                    \
    preempt_enable();                           \
  } while (0)

#define __lock_irqsafe(type, s...)              \
  do {                                          \
    interrupts_disable();                       \
    arch_spinlock_##type(s);                    \
  } while (0)

#define __unlock_irqsafe(type, s...)            \
  do {                                          \
    arch_spinlock_##type(s);                    \
    interrupts_enable();                        \
  } while (0)

#define __lock_irqsave(type, v, s...)           \
  do {                                          \
    (v) = is_interrupts_enabled();              \
    __lock_irqsafe(type, s);                    \
  } while (0)

#define __unlock_irqrestore(type, v, s...)      \
  do {                                          \
    arch_spinlock_##type(s);                    \
    if (v) {                                    \
      interrupts_enable();                      \
    }                                           \
  } while (0)

/* TODO DK: add trylock and islock for RW locks, irq locks */
#define spinlock_trylock(s)                     \
  ({ bool isok; preempt_disable();              \
     isok = spinlock_trylock(s);                \
     if (!isok)                                 \
       preempt_enable();                        \
     isok; })

#define spinlock_is_locked(s) arch_spinlock_is_locked(s)

/* CPU-bound spinlocks. */
#define bound_spinlock_initialize(b,cpu)        \
  do {                                          \
    (b)->__lock=__SPIN_LOCK_UNLOCKED;           \
    (b)->__cpu=cpu;                             \
  } while(0)

#define bound_spinlock_lock_cpu(b,cpu)          \
  preempt_disable();                            \
  arch_bound_spinlock_lock_cpu((b),cpu)

#define bound_spinlock_unlock(b)                \
  arch_bound_spinlock_unlock((b));              \
  preempt_enable()

#define bound_spinlock_trylock_cpu(b,cpu)               \
  ({bool is_ok;preempt_disable();                       \
  is_ok=arch_bound_spinlock_trylock_cpu((b),cpu);       \
  if(!is_ok) {preempt_enable();}                        \
  is_ok; })

#else

/* FIXME DK: He-he-he, it seems all this stuff wouldn't built properly if SMP is disabled (: */
#define SPINLOCK_INITIALIZE(state)
#define RW_SPINLOCK_INITIALIZE(state)
#define SPINLOCK_DEFINE(s)    spinlock_t (s)
#define RW_SPINLOCK_DEFINE(s) rw_spinlock_t (s)

#define spinlock_initialize(x)
#define rw_spinlock_initialize(x)

#define __lock_spin(type, s...) preempt_disable()
#define __unlock_spin(type, s...) preempt_enable()
#define __lock_irqsafe(type, s...) interrupts_disable()
#define __unlock_irqsafe(type, s...) interrupts_enable()

#define __lock_irqsave(type, v, s...)           \
  do {                                          \
    (v) = is_interrupts_enabled();              \
    interrupts_disable();                       \
  } while (0)

#define __unlock_irqrestore(type, v, s...)      \
  if (v) {                                      \
    interrupts_enable();                        \
  }

#define spinlock_trylock(s)  (true)
#define spinlock_is_locked(s) (false) 

#endif /* CONFIG_SMP */

/* locking functions */
#define spinlock_lock(s)                        \
  __lock_spin(lock, s)
#define spinlock_lock_read(s)                   \
  __lock_spin(lock_read, s)
#define spinlock_lock_write(s)                  \
  __lock_spin(lock_write, s)
#define spinlock_lock_bit(bitmap, bit)          \
  __lock_spin(lock_bit, bitmap, bit)
#define spinlock_lock_irqsafe(s)                \
  __lock_irqsafe(lock, s)
#define spinlock_lock_read_irqsafe(s)           \
  __lock_irqsafe(lock_read, s)
#define spinlock_lock_write_irqsafe(s)          \
  __lock_irqsafe(lock_write, s)
#define spinlock_lock_bit_irqsafe(bitmap, bit)  \
  __lock_irqsafe(lock_bit, bitmap, bit)
#define spinlock_lock_irqsave(s, v)             \
  __lock_irqsave(lock, v, s)
#define spinlock_lock_read_irqsave(s, v)        \
  __lock_irqsave(lock_read, v, s)
#define spinlock_lock_write_irqsave(s)          \
  __lock_irqsave(lock_write, v, s)
#define spinlock_lock_bit_irqsave(bitmap, bit, v)   \
  __lock_irqsave(lock_bit, v, bitmap, bit)

/* unlocking functions */
#define spinlock_unlock(s)                      \
  __unlock_spin(unlock, s)
#define spinlock_unlock_read(s)                 \
  __unlock_spin(unlock_read, s)
#define spinlock_unlock_write(s)                \
  __unlock_spin(unlock_write, s)
#define spinlock_unlock_bit(bitmap, bit)        \
  __unlock_spin(unlock_bit, bitmap, bit)
#define spinlock_unlock_irqsafe(s)              \
  __unlock_irqsafe(unlock, s)
#define spinlock_unlock_read_irqsafe(s)         \
  __unlock_irqsafe(unlock_read, s)
#define spinlock_unlock_write_irqsafe(s)        \
  __unlock_irqsafe(unlock_write, s)
#define spinlock_unlock_bit_irqsafe(bitmap, bit)    \
  __unlock_irqsafe(unlock_bit, bitmap, bit)
#define spinlock_unlock_irqrestore(s, v)        \
  __unlock_irqrestore(unlock, v, s)
#define spinlock_unlock_read_irqrestore(s, v)   \
  __unlock_irqrestore(unlock_read, v, s)
#define spinlock_unlock_write_irqrestore(s, v)  \
  __unlock_irqrestore(unlock_write, v, s)
#define spinlock_unlock_bit_irqrestore(bitmap, bit, v)  \
  __unlock_irqrestore(unlock_bit, v, bitmap, bit)

#endif /* __EZA_SPINLOCK_H__ */

