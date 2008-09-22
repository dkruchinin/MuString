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
 * include/eza/amd64/spinlock.h: spinlock and atomic amd64 specific and 
 *                               extended functions
 *
 */

#ifndef __AMD64_SPINLOCK_H__
#define __AMD64_SPINLOCK_H__

#include <eza/arch/types.h>
#include <eza/spinlock.h>
#include <eza/arch/mbarrier.h>

#define atomic_pre_inc(v)  (atomic_post_inc(v)+1)
#define atomic_pre_dec(v)  (atomic_post_dec(v)-1)

static inline void atomic_inc(atomic_t *v)
{
  asm volatile("incq %0" : "=m" (v->c));
}

static inline void atomic_dec(atomic_t *v)
{
  asm volatile ("decq %o\n" : "=m" (v->c));
}

static inline long atomic_post_inc(atomic_t *v)
{
  long r=1;

  asm volatile ("lock xaddr %1, %0\n" : "=m" (v->c),"+r" (r));

  return r;
}

static inline long atomic_post_dec(atomic_t *v)
{
  long r=-1;

  asm volatile ("lock xaddr %1, %0" : "=m" (v->c), "+r" (r));

  return r;
}

static inline uint64_t atomic_test_set(atomic_t *v)
{
  uint64_t o;

  asm volatile ("movq $1, %0\n"
		"xchgq %0, %1\n" : "=r" (o), "=m" (v->c));

  return o;
}

/* amd64 specific fast spinlock */
static inline void arch_atomic_spinlock1(atomic_t *v)
{
  uint64_t t;

  /* FIXME: disable preemtion on current task */

  asm volatile (
                "0:;"
#ifdef CONFIG_HT
                "pause;"
#endif
                "mov %0, %1;"
                "testq %1, %1;"
                "jnz 0b;"
                "incq %1;"
                "xchgq %0, %1;"
                "testq %1, %1;"
		"jnz 0b;"
                : "=m"(v->count),"=r"(t)
                );

  barrier_enter();
}

/*
static inline void arch_atomic_spinlock(atomic_t *v)
{
  //  v->count = 1;
}
*/

#endif /* __AMD64_SPINLOCK_H__ */

