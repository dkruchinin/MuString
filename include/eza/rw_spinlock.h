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
 * (c) Copyright 2008 Michael Tsymbalyuk <mtzaurus@gmail.com>
 *
 * include/eza/rw_spinlock.h: generic prototypes for read-write spinlocks.
 *
 */

#ifndef __EZA_RW_SPINLOCK_H__
#define __EZA_RW_SPINLOCK_H__

#include <config.h>
#include <eza/arch/atomic.h>
#include <eza/arch/types.h>
#include <eza/arch/preempt.h>
#include <eza/arch/interrupt.h>

typedef struct __rw_spinlock_type {
    lock_t __r, __w;
} rw_spinlock_t;

#define RW_SPINLOCK_DEFINE(s) rw_spinlock_t s = {     \
        .__r = 0,                                  \
        .__w = 0,                                  \
    };

#ifdef CONFIG_SMP

#include <eza/arch/rw_spinlock.h>

#define rw_spinlock_initialize(x,y)                \
    ((rw_spinlock_t *)x)->__r=0;((rw_spinlock_t *)x)->__w=0

#define rw_spinlock_lock_read(u) \
  preempt_disable(); \
  arch_rw_spinlock_lock_read(u);

#define rw_spinlock_lock_write(u) \
  preempt_disable(); \
  arch_rw_spinlock_lock_write(u);

#define rw_spinlock_unlock_read(u) \
  arch_rw_spinlock_unlock_read(u); \
  preempt_enable();

#define rw_spinlock_unlock_write(u) \
  arch_rw_spinlock_unlock_write(u); \
  preempt_enable();

#else

#define rw_spinlock_initialize(x, name)
#define rw_spinlock_lock_read(x) preempt_disable()
#define rw_spinlock_lock_write(x) preempt_disable()
#define rw_spinlock_unlock_read(x) preempt_enable()
#define rw_spinlock_unlock_write(x) preempt_enable()

#endif

#endif
