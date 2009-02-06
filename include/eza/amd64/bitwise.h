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
 * include/eza/arch/bitwise.h: AMD64-specific bitwise operations
 *
 */

/**
 * @file include/eza/arch/bitwise.h
 * A set of AMD64-dependent functions providing bitwise
 * operations API.
 *
 * @author Dan Kruchinin
 */

#ifndef __ARCH_BITWISE_H__
#define __ARCH_BITWISE_H__

#include <mlibc/types.h>

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
static always_inline int arch_bit_test(volatile void *bitmap, int bitno)
{
  int ret;

  __asm__ volatile ("bt %1, %2\n\t"
                    "sbb %0, %0"
                    : "=r" (ret)
                    : "Ir" (bitno), "m" (*(volatile unsigned long *)bitmap));

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

#define ARCH_BIT_FIND_LSF
static always_inline long arch_bit_find_lsf(unsigned long word)
{
  __asm__ ("bsf %1, %2\n\t"
           : "=r" (word)
           : "r" (word), "r" ((long)-1));

  return word;
}

#define ARCH_ZERO_BIT_FIND_LSF
static always_inline long arch_zero_bit_find_lsf(unsigned long word)
{
  __asm__ ("bsf %1, %2\n\t"
           : "=r" (word)
           : "r" (~word), "r" ((long)-1));

  return word;
}

#define ARCH_BIT_FIND_MSF
static always_inline long arch_bit_find_msf(unsigned long word)
{
  __asm__ ("bsr %1, %2\n\t"
           : "=r" (word)
           : "r" (word), "r" ((long)-1));
  return word;
}

#define ARCH_ZERO_BIT_FIND_MSF
static always_inline long arch_zero_bit_find_msf(unsigned long word)
{
  __asm__ ("bsr %1, %2\n\t"
           : "=r" (word)
           : "r" (~word), "r" ((long)-1));
  return word;
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

#endif /* __ARCH_BITWISE_H__ */
