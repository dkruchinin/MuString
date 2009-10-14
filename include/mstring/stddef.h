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
 *
 */

/**
 * @file include/mlibc/stddef.h
 * Contains widely used macros.
 */

#ifndef __MSTRING_STDDEF_H__
#define __MSTRING_STDDEF_H__

#include <mstring/bitwise.h>
#include <mstring/types.h>

#ifndef offsetof
/**
 * @def offsetof(st, m)
 * @brief offset of member with name @a m in the structure @a st
 * @param st - a type of structure
 * @param m  - name of member in the @a st
 * @return offset of @a m in the structure @st
 */
#define offsetof(st, m)                         \
  ((char *)&((st *)0)->m - (char *)(st *)0)
#endif /* offsetof */


/**
 * @def container_of(ptr, type, member)
 * @brief Get a pointer to member's parent structure
 * @param ptr    - a pointer to member
 * @param type   - the type of member's parent
 * @param member - name of member in @a type structure.
 * @return parent's address
 */
#define container_of(ptr, type, member) \
  (type *)((char *)(ptr) - offsetof(type, member))

#define round_up(a, b) ((((a) + ((b) - 1)) / (b)) * (b))
#define align_up(s,a)    (((s)+((a)-1)) & ~((a)-1))
#define align_down(s,a)  ((s) & ~((a)-1))
#define round_up_pow2(x) (!is_powerof2(x) ? (1 << (bit_find_msf(x) + 1)) : (x))
#define round_down_pow2(x) (!is_poweofr2(x) ? (1 << bit_find_msf(x)) : x)

#define is_powerof2(num) (((num) & ~((num) - 1)) == (num))
#define ABS(x) (((x) < 0) ? -(x) : (x))
#define MIN(a,b) ((a)<(b) ? (a) : (b) )
#define MAX(a,b) ((a)>(b) ? (a) : (b) )
#define BITNUM(po2)    bit_find_msf(po2)
#define ARRAY_SIZE(a)  (sizeof((a))/sizeof((a)[0]))

/**
 * @def POW2(n)
 * @brief Get @a n's power of two.
 * @param n - A power 2 will be reised
 * @return @a n's power of 2.
 */
#define POW2(n) (1 << (n))

#define B2KB(b)   ((b) >> 10)
#define B2MB(b)   ((b) >> 20)
#define KB2B(kb)  ((kb) << 10)
#define KB2MB(kb) ((kb) >> 10)
#define MB2B(mb)  ((mb) << 20)
#define MB2KB(mb) ((mb) << 10)

#endif /* __STDDEF_H__ */
