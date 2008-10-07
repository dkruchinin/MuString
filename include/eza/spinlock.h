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
 *
 * include/eza/spinlock.h: spinlock and atomic types and functions
 *                         architecture independed
 *
 */

#ifndef __EZA_SPINLOCK_H__ /* there are several spinlock.h with different targets */
#define __EZA_SPINLOCK_H__

#include <config.h>
#include <eza/arch/atomic.h>
#include <eza/arch/types.h>
#include <eza/arch/mbarrier.h>
#include <eza/arch/preempt.h>
#include <eza/arch/interrupt.h>
#include <eza/arch/spinlock.h>

#ifdef CONFIG_SMP

/* simple spinlocks... */
#define mbarrier_leave()

#define spinlock_declare(s)  spinlock_t s
#define spinlock_extern(s)   extern spinlock_t s

#define spinlock_initialize(x,y)                \
  (((spinlock_t *)x)->__spin_val = __SPINLOCK_UNLOCKED_V)

#define rw_spinlock_initialize(x,y)                         \
  (((rw_spinlock_t *)x)->__r=0;((rw_spinlock_t *)x)->__w=0)

#define SPINLOCK_DEFINE(s) spinlock_t s = {     \
    .__spin_val = __SPINLOCK_UNLOCKED_V,        \
  }

#define RW_SPINLOCK_DEFINE(s) rw_spinlock_t s = {     \
    .__r = 0,                                         \
    .__w = 0,                                         \
  };

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

/* TODO DK: add trylock and islock for RW locks, irq locks */
#define spinlock_trylock(s)                     \
  ({ bool isok; preempt_disable();              \
     isok = spinlock_trylock(s);                \
     if (!isok)                                 \
       preempt_enable();                        \
     isok; })

#define spinlock_is_locked(s) arch_spinlock_is_locked(s)

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
#define spinlock_unlock_bit_irqsafe(unlock, bitmap, bit)    \
  __unlock_irqsafe(unlock_bit, bitmap, bit)

#else

/* FIXME DK: He-he-he, it seems all this stuff wouldn't built properly if SMP is disabled (: */

/* simple spinlcocks */
#define SPINLOCK_DEFINE(s)
#define spinlock_declare(s)  
#define spinlock_extern(s)   
#define spinlock_initialize_macro(s)  

#define spinlock_initialize(x, name)
#define spinlock_lock(x) preempt_disable()
#define spinlock_trylock(x) /* the same like above */
#define spinlock_unlock(x) preempt_enable()

/* IRQ-safe */
#define spinlock_lock_irqsafe(s)
#define spinlock_unlock_irqsafe(s)

/* RW-spinlocks */
#define RW_SPINLOCK_DEFINE(x)
#define rw_spinlock_initialize(x, name)
#define rw_spinlock_lock_read(x) preempt_disable()
#define rw_spinlock_lock_write(x) preempt_disable()
#define rw_spinlock_unlock_read(x) preempt_enable()
#define rw_spinlock_unlock_write(x) preempt_enable()

#endif /* CONFIG_SMP */
#endif /* __EZA_SPINLOCK_H__ */

