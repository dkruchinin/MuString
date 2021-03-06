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
 * (c) Copyright 2008 Dan Kruchinin <dan.kruchinin@gmail.com>
 *
 * include/arch/atomic.h: AMD64-specific atomic operations
 *
 */

/**
 * @file include/arch/atomic.h
 * A set of AMD64-dependent dependent atomic operations.
 *
 * @author Dan Kruchinin
 */

#ifndef __ARCH_ATOMIC_H__
#define __ARCH_ATOMIC_H__

#include <arch/bitwise.h>
#include <mstring/types.h>

typedef volatile ulong_t atomic_t;

/**
 * @def atomic_set(a, val)
 * Set a value of an atomic variable @a a
 *
 * @param a   - A pointer to atomic variable
 * @param val - The value to set.
 */
#define atomic_set(a, val) (*(a) = (val))

/**
 * @def atomic_get(a)
 * Get a value of an atomic variable @a a
 *
 * @param a - A pointer to atomic variable
 * @return The value of atomic variable @a a
 */
#define atomic_get(a) (*(a))

/**
 * @fn static always_inline void atomic_add(atomic_t *a, long add)
 * Atomically add signed number @a add to an atomic variable @a a
 *
 * @param[out] a   - A pointer to atomic variable @a add will be added to
 * @param[in]  add - A signed number to add.
 */
static always_inline void atomic_add(atomic_t *a, long add)
{
    __asm__ volatile (__LOCK_PREFIX "addq %1, %0\n\t"
                      : "+m" (*a)
                      : "ir" (add));
}

/**
 * @fn static always_inline void atomic_inc(atomic_t *a)
 * Atomically increment the value of atomic variable @a a
 *
 * @param[out] a - A pointer to atomic variable.
 */
static always_inline void atomic_inc(atomic_t *a)
{
  __asm__ volatile (__LOCK_PREFIX "incq %0\n\t"
                    : "+m" (*a));
}

/**
 * @fn static always_inline bool atomic_sub_and_test(atomic_t *a,long sub)
 * Atomically decrements target atomic variable by a given amount of bytes
 * and tests if its
 * value is zero.
 * @return true - if atomic variable is zero, false - if not.
 */
static always_inline bool atomic_inc_and_test(atomic_t *a)
{
  uint8_t ret;
  __asm__ volatile (__LOCK_PREFIX "incq %0; sete %1"
                    : "+m" (*a), "=m" (ret));

  return (ret != 0);
}

/**
 * @fn static always_inline void atomic_sub(atomic_t *a, long sub)
 * Atomically substract signed number @a sub from an atomic variable @a a
 *
 * @param[out] a   - A pointer to atomic vatiable @a sub will be substracted from.
 * @param[in]  sub - A signed number to substract.
 */
static always_inline void atomic_sub(atomic_t *a, long sub)
{
  __asm__ volatile (__LOCK_PREFIX "subq %1, %0\n\t"
                    : "+m" (*a)
                    : "ir" (sub));
}

static always_inline bool atomic_sub_and_test(atomic_t *a, long sub)
{
  uint8_t ret;
  __asm__ volatile (__LOCK_PREFIX "subq %2, %0\n\t"
                    "sete %1"
                    : "+m" (*a), "=m" (ret)
                    : "ir" (sub));

  return ret;
}

/**
 * @fn static always_inline void atomic_dec(atomic_t *a)
 * Atomically decrement the value of atomic variable @a a
 *
 * @param[out] a - A pointer to atomic variable.
 */
static always_inline void atomic_dec(atomic_t *a)
{
  __asm__ volatile (__LOCK_PREFIX "decq %0\n\t"
                    : "+m" (*a));
}

/**
 * @fn static always_inline bool atomic_dec_and_test(atomic_t *a)
 * Atomically decrements target atomic variable and tests if its
 * value is zero.
 * @return true - if atomic variable is zero, false - if not.
 */
static always_inline bool atomic_dec_and_test(atomic_t *a)
{
  uint8_t ret;
  __asm__ volatile (__LOCK_PREFIX "decq %0\n\t"
                    "sete %1"
                    : "+m" (*a), "=m" (ret));

  return (ret != 0);
}

#define atomic_bit_set(bitmap, bit)             \
  arch_bit_set(bitmap, bit)
#define atomic_bit_clear(bitmap, bit)           \
  arch_bit_clear(bitmap, bit)
#define atomic_bit_toggle(bitmap, bit)          \
  arch_bit_toggle(bitmap, bit)
#define atomic_bit_test(bitmap, bit)            \
  arch_bit_test(bitmap, bit)
#define atomic_test_and_set_bit(bitmap, bit)    \
  arch_bit_test_and_set(bitmap, bit)
#define atomic_test_and_clear_bit(bitmap, bit)  \
  arch_bit_test_and_clear(bitmap, bit)

#endif /* __ARCH_ATOMIC_H__ */
