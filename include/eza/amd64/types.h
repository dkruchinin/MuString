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
 * include/eza/amd64/types.h: types definions
 *
 */

#ifndef __ARCH_TYPES_H__
#define __ARCH_TYPES_H__

#include <config.h>

typedef signed char int8_t;
typedef signed short int16_t;
typedef signed int int32_t;
typedef signed long long int64_t;
typedef unsigned char uint8_t; /* unsigned */
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;
typedef unsigned long uintptr_t;
typedef int32_t status_t;

/* bit-related types. */
typedef uint32_t bit_idx_t;

#define TYPE_LONG_SHIFT  6
#define BITS_PER_LONG  64

#ifdef CONFIG_SMP
/*
 * x86 and x86_64(amd64) architectures provide lock prefix
 * that guaranty atomic execution limited set of operations
 * such as:
 * ADC, ADD, AND, BTC, BTR, BTS, CMPXCHG, CMPXCHG8B, CMPXCHG16B, DEC,
 * INC, NEG, NOT, OR, SBB, SUB, XADD, XCHG, and XOR
 * (list of operations supporting lock prefix was taken from amd64 manual,
 * volume 3)
 */
#define __LOCK_PREFIX "lock "
#else
#define __LOCK_PREFIX
#endif /* CONFIG_SMP */

#endif /* __ARCH_TYPES_H__ */
