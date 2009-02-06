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
 * include/mlibc/bitwise.h: Architecture independent bitwise operations
 *
 */

/**
 * @file include/mlibc/bitwise.h
 * Architecure independent bitwise operations API.
 *
 * @author Dan Kruchinin
 */

#ifndef __BITWISE_H__
#define __BITWISE_H__

#include <eza/arch/bitwise.h>

/**
 * @fn static inline void bit_set(void *bitmap, int bit)
 * Set bit number @a bit in the bitmap @a bitmap
 * @note The limit of @a bitmap = sizeof(unsigned long)
 *
 * @param[out] bitmap - A pointer to the bitmap
 * @param bit - Number of bit to set
 */
#ifndef ARCH_BIT_SET
static inline void bit_set(void *bitmap, int bit)
{
  *(unsigned long *)bitmap |= (1 << bit);
}
#else
#define bit_set(bitmap, bit) arch_bit_set(bitmap, bit)
#endif /* ARCH_BIT_SET */

/**
 * @fn static inline void bit_clear(void *bitmap, int bit)
 * Clear bit number @a bit in the bitmap @a bitmap
 * @note The limit of @a bitmap = sizeof(unsigned long)
 *
 * @param[out] bitmap - A pointer to the bitmap
 * @param bit - Number of bit to clear
 */
#ifndef ARCH_BIT_CLEAR
static inline void bit_clear(void *bitmap, int bit)
{
  *(unsigned long *)bitmap &= ~(1 << bit);
}
#else
#define bit_clear(bitmap, bit) arch_bit_clear(bitmap, bit)
#endif /* ARCH_BIT_CLEAR */

/**
 * @fn static inline void bit_toggle(void *bitmap, int bit)
 * Toggle bit @a bit in the bitmap @a bitmap.
 * @note The limit of @a bitmap = sizeof(unsigned long)
 *
 * @param[out] bitmap - A pointer to the bitmap.
 * @param bit - Number of bit to toggle
 */
#ifndef ARCH_BIT_TOGGLE
static inline void bit_toggle(void *bitmap, int bit)
{
  *(unsigned long *)bitmap ^= (1 << bit);
}
#else
#define bit_toggle(bitmap, bit) arch_bit_toggle(bitmap, bit)
#endif /* ARCH_BIT_TOGGLE */

/**
 * @fn static inline int bit_test(void *bitmap, int bitno)
 * Test if bit with number @a bitno is set in the bitmap @a bitmap.
 * @note The limit of @a bitmap = sizeof(unsigned long)
 *
 * @param bitmap - A pointer to the bitmap.
 * @param bitno - Number of bit to test
 * @return 1 if bit is set and 0 otherwise
 */
#ifndef ARCH_BIT_TEST
static inline int bit_test(void *bitmap, int bitno)
{
  return ((*(unsigned long *)bitmap & (1 << bitno)) >> bitno);
}
#else
#define bit_test(bitmap, bitno) arch_bit_test(bitmap, bitno)
#endif /* ARCH_BIT_TEST */

/**
 * @fn static inline int bit_test_and_set(void *bitmap, int bitno)
 * @brief Get old value of bit with number @a bitno and set @a bitno bit in the bitmap
 *
 * This function is similar to bit_set() except it copies old bit value before
 * setting it to 1 and return that value after needed bit was setted.
 * @note The limit of @a bitmap = sizeof(unsigned long)
 *
 * @param bitmap - A pointer to the bitmap
 * @param bitno  - The number of bit to test and set
 * @return Old value of bit with number @a bitno
 */
#ifndef ARCH_BIT_TEST_AND_SET
static inline int bit_test_and_set(void *bitmap, int bitno)
{
  int val = (*(unsigned long *)bitmap & (1 << bitno));
  *(unsigned long *)bitmap |= (1 << bitno);

  return val;
}
#else
#define bit_test_and_set(bitmap, bitno) arch_bit_test_and_set(bitmap, bitno)
#endif /* ARCH_BIT_TEST_AND_SET */

/**
 * @fn static inline long bit_find_lsf(unsigned long word)
 * Find first set least significant bit.
 *
 * @param word - Where to search
 * @return Found bit number on success, negative value on failure.
 */
#ifndef ARCH_BIT_FIND_LSF
static inline long bit_find_lsf(unsigned long word)
{
  long c = -1;

  for (; word; c++, word >>= 1) {
    if ((word & 0x01)) {
      c++;
      break;
    }      
  }
  
  return c;
}
#else
#define bit_find_lsf(word) arch_bit_find_lsf(word)
#endif /* ARCH_BIT_FIND_LSF */

#ifndef ARCH_ZERO_BIT_FIND_LSF
#define zero_bit_find_lsf(word) bit_find_lsf(~(word))
#else
#define zero_bit_find_lsf(word) arch_zero_bit_find_lsf(word)
#endif /* ARCH_ZERO_BIT_FIND_LSF */

/**
 * @fn static inline long bit_find_msf(unsigned long word)
 * Find most significant set bit in the @a word.
 *
 * @param word - Where to search.
 * @return Found bit number on success, negative value on failure.
 */
#ifndef ARCH_BIT_FIND_MSF
static inline long bit_find_msf(unsigned long word)
{
  long c = -1;

  while (word) {
    c++;
    word >>= 1;
  }

  return c;
}
#else
#define bit_find_msf(word) arch_bit_find_msf(word)
#endif /* ARCH_BIT_FIND_MSF */

#ifndef ARCH_ZERO_BIT_FIND_MSF
#define zero_bit_find_msf(word) bit_find_msf(~(word))
#else
#define zero_bit_find_msf(word) arch_zero_bit_find_msf(word)
#endif /* ARCH_ZERO_BIT_FIND_MSF */

/**
 * @fn static inline void bits_or(void *word, unsigned long flags)
 * Executes logical OR with @a word and @a flags and writes result to @a word
 * @note The limit of @a word = sizeof(unsigned long)
 *
 * @param[out] word - A pointer to memory results will be written to
 * @param flsgs - Flags that will be OR'ed with @a word
 */
#ifndef ARCH_BITS_OR
static inline void bits_or(void *word, unsigned long flags)
{
  *(unsigned long *)word |= flags;
}
#define bits_or(word, flags) panic("bits_or uniplemented")
#else
#define bits_or(word, flags) arch_bits_or(word, flags)
#endif /* ARCH_BITS_OR */

/**
 * @fn static inline void bits_and(void *word, unsigned long mask)
 * Executes logical AND with @a word and @a mask and writes result to @a word.
 * @note The limit of @a word = sizeof(unsigned long)
 *
 * @param[out] word - A pointer to memory results will be written to
 * @param mask - A mask that will be AND'ed with @a word
 */
#ifndef ARCH_BITS_AND
static inline void bits_and(void *word, unsigned long mask)
{
  *(unsigned long *)word &= mask;
}
#else
#define bits_and(word, mask) arch_bits_and(word, mask)
#endif /* ARCH_BITS_AND */

#endif /* __BITWISE_H__ */
