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
 * (c) Copyright 2008 Dan Kruchinin <dan.kruchinin@gmail.com>
 *
 * include/eza/arch/atomic.h: AMD64-specific atomic operations
 *
 */

#ifndef __AMD64_ATOMIC_H__
#define __AMD64_ATOMIC_H__

#include <config.h>
#include <eza/arch/asm.h>
#include <eza/arch/types.h>

typedef volatile long atomic_t;

#define atomic_set(a, val) (*(a) = (val))
#define atomic_get(a) (*(a))

static inline void atomic_add(atomic_t *a, long add)
{
  __asm__ volatile ( __LOCK_PREFIX "addq %1, %0\n\t"
                    : "+m" (*a)
                    : "ir" (add));
}

static always_inline void atomic_inc(atomic_t *a)
{
  __asm__ volatile (__LOCK_PREFIX "incq %0\n\t"
                    : "+m" (*a));
}

static always_inline void atomic_sub(atomic_t *a, long sub)
{
  __asm__ volatile (__LOCK_PREFIX "subq %1, %0\n\t"
                    : "+m" (*a)
                    : "ir" (sub));
}

static always_inline void atomic_dec(atomic_t *a)
{
  __asm__ volatile (__LOCK_PREFIX "decq %0\n\t"
                    : "+m" (*a));
}

#endif /* __AMD64_ATOMIC_H__ */
