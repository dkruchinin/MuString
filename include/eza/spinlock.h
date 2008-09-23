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

#define __SPINLOCK_LOCKED_V 1
#define __SPINLOCK_UNLOCKED_V 0

typedef struct __spinlock_type {
    long_t __spin_val;
} spinlock_t;

#ifdef CONFIG_SMP

#include <eza/arch/spinlock.h>

#define spinlock_trylock(x)
#define mbarrier_leave()

#define spinlock_declare(s)  spinlock_t s
#define spinlock_extern(s)   extern spinlock_t s

#define spinlock_initialize(x,y)                \
    ((spinlock_t *)x)->__spin_val = __SPINLOCK_UNLOCKED_V

#define spinlock_lock(u) \
  preempt_disable(); \
  arch_spinlock_lock(u);

#define spinlock_unlock(u) \
  arch_spinlock_unlock(u); \
  preempt_enable();

#else

#define spinlock_declare(s)  
#define spinlock_extern(s)   
#define spinlock_initialize_macro(s)  

#define spinlock_initialize(x, name)
#define spinlock_lock(x) preempt_disable()
#define spinlock_trylock(x) /* the same like above */
#define spinlock_unlock(x) preempt_enable()

#endif /* CONFIG_SMP */
#endif /* __EZA_SPINLOCK_H__ */

