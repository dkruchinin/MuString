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
 * include/eza/bits.h: Contains main types and prototypes for dealins with bits.
 *
 */

#ifndef __BITS_H__
#define __BITS_H__ 

#include <eza/bits.h>

#define INVALID_BIT_INDEX  ~((bit_idx_t)0) /* Bit that never exists */

static inline bit_idx_t find_first_bit32( uint32_t v );
static inline bit_idx_t find_first_bit64( uint64_t v );

static inline bit_idx_t find_first_bit_mem_64( uint64_t *ptr, uint64_t count );

static inline bit_idx_t reset_and_test_bit_mem_64( uint64_t *ptr, bit_idx_t bit );
static inline bit_idx_t set_and_test_bit_mem_64( uint64_t *ptr, bit_idx_t bit );

#endif

