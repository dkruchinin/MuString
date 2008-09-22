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
 * include/mlibc/bitwise.h: Architecture independent bitwise operations
 *
 */

#ifndef __BITWISE_H__
#define __BITWISE_H__

#include <eza/arch/bitwise.h>

#ifndef ARCH_BIT_SET
static inline void bit_set(void *bitmap, int bit)
{
  *(char *)bitmap |= (1 << bit);
}
#else
#define bit_set(bitmap, bit) arch_bit_set(bitmap, bit)
#endif /* ARCH_BIT_SET */

#ifndef ARCH_BIT_CLEAR
static inline void bit_clear(void *bitmap, int bit)
{
  *(char *)bitmap &= ~(1 << bit);
}
#else
#define bit_clear(bitmap, bit) arch_bit_clear(bitmap, bit)
#endif /* ARCH_BIT_CLEAR */

#ifndef ARCH_BIT_TOGGLE
static inline void bit_toggle(void *bitmap, int bit)
{
  *(char *)bitmap ^= (1 << bit);
}
#else
#define bit_toggle(bitmap, bit) arch_bit_toggle(bitmap, bit)
#endif /* ARCH_BIT_TOGGLE */

#ifndef ARCH_BIT_TEST
static inline int bit_test(void *bitmap, int bitno)
{
  return (*(char *)bitmap & (1 << bitno));
}
#else
#define bit_test(bitmap, bitno) arch_bit_test(bitmap, bitno)
#endif /* ARCH_BIT_TEST */

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

#ifndef ARCH_BITS_OR
#define bits_or(word, flags) panic("bits_or uniplemented")
#else
#define bits_or(word, flags) arch_bits_or(word, flags)
#endif /* ARCH_BITS_OR */

#ifndef ARCH_BITS_AND
#define bits_and(word, mask) panic("bits_and unimplemented")
#else
#define bits_and(word, mask) arch_bits_and(word, mask)
#endif /* ARCH_BITS_AND */

#endif /* __BITWISE_H__ */
