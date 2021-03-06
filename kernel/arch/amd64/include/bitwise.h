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
 * AMD64-specific bitwise operations.
 */

/**
 * @file include/arch/bitwise.h
 * A set of AMD64-dependent functions providing bitwise
 * operations API.
 *
 * @author Dan Kruchinin
 */

#ifndef __MSTRING_ARCH_BITWISE_H__
#define __MSTRING_ARCH_BITWISE_H__

#include <mstring/types.h>

#define ARCH_BIT_SET
/* Atomic operation */
static always_inline void arch_bit_set(volatile void *bitmap, int bit)
{
  __asm__ volatile (__LOCK_PREFIX "bts %1, %0\n\t"
                    :: "m" (*(volatile long *)bitmap), "Ir" (bit));
}

#define ARCH_BIT_CLEAR
/* Atomic operation */
static always_inline void arch_bit_clear(volatile void *bitmap, int bit)
{
  __asm__ volatile (__LOCK_PREFIX "btr %1, %0\n\t"
                    : "=m" (*(volatile long *)bitmap)
                    : "Ir" (bit)
                    : "memory");
}

#define ARCH_BIT_TOGGLE
/* atomic */
static always_inline void arch_bit_toggle(volatile void *bitmap, int bit)
{
  __asm__ volatile (__LOCK_PREFIX "btc %1, %0\n\t"
                    : "=m" (*(volatile long *)bitmap)
                    : "Ir" (bit)
                    : "memory");
}

#define ARCH_BIT_TEST
/* not atomic */
static inline int arch_bit_test(volatile void *bitmap, long bitno)
{
  int ret = 0;

  __asm__ volatile ("btq %1, %2\n\t"
                    "sbbl %0, %0\n"
                    : "+r" (ret)
                    : "ir" (bitno), "m" (*(volatile ulong_t *)bitmap));

  return ret;
}

#define ARCH_BIT_TEST_AND_SET
/* Atomic operation */
static always_inline int arch_bit_test_and_set(volatile void *bitmap, int bit)
{
  int val;  
  __asm__ volatile ( __LOCK_PREFIX "bts %2, %0\n\t"
                     "sbb %1, %1"
                     : "=m" (*(volatile long *)bitmap), "=r" (val)
                     : "Ir" (bit)
                     : "memory");
  return val;
}

#define ARCH_BIT_TEST_AND_CLEAR
static always_inline int arch_bit_test_and_clear(volatile void *bitmap, int bit)
{
  int val;  
  __asm__ volatile ( __LOCK_PREFIX "btc %2, %0\n\t"
                     "sbb %1, %1"
                     : "=m" (*(volatile long *)bitmap), "=r" (val)
                     : "Ir" (bit)
                     : "memory");
  return val;
}

#define ARCH_BIT_FIND_LSF
static inline long arch_bit_find_lsf(unsigned long word)
{
  long ret = -1;
  
  __asm__ volatile ("bsf %1, %0\n"
                    : "+r" (ret)
                    : "m" (word));

  return ret;
}

#define ARCH_ZERO_BIT_FIND_LSF
static always_inline long arch_zero_bit_find_lsf(unsigned long word)
{
  long ret = -1;
  
  __asm__ volatile ("bsf %1, %0\n"
                    : "+r" (ret)
                    : "r" (~word));

  return ret;
}

#define ARCH_BIT_FIND_MSF
static always_inline long arch_bit_find_msf(unsigned long word)
{
  long ret = -1;
  
  __asm__ volatile ("bsr %1, %0\n"
                    : "+r" (ret)
                    : "m" (word));
  
  return ret;
}

#define ARCH_ZERO_BIT_FIND_MSF
static always_inline long arch_zero_bit_find_msf(unsigned long word)
{
  long ret = -1;
  
  __asm__ volatile ("bsr %1, %0\n\t"
                    : "+r" (ret)
                    : "r" (~word));
  return ret;
}

#define ARCH_BITS_OR
/* atomic operation */
static always_inline void arch_bits_or(volatile void *word, unsigned long flags)
{
  __asm__ volatile (__LOCK_PREFIX "or %1, %0\n\t"
                    : "+m" (*(volatile unsigned long *)word)
                    : "r" (flags)
                    : "memory");
}

#define ARCH_BITS_AND
/* atomic operation */
static always_inline void arch_bits_and(volatile void *word, unsigned long mask)
{
  __asm__ volatile (__LOCK_PREFIX "and %1, %0\n\t"
                    : "+m" (*(volatile unsigned long *)word)
                    : "r" (mask)
                    : "memory");
}

#endif /* __MSTRING_ARCH_BITWISE_H__ */
