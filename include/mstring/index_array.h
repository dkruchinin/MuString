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
 * (c) Copyright 2008 Michael Tsymbalyuk <mtzaurus@gmail.com>
 *
 * mstring/index_array.h: prototypes for the index array implementation.
 */

/**
 * @file mstring/index_array.c
 *
 * Implements 'index array' - an object that manages a range of values and
 * that is able to retrieve the next available value from the range at
 * constant time. It is also puts unused values back to the array making
 * them available for the future retrievals.
 * Ranges always start from '0' and end at user-defined value passed during
 * the array initialization step.
 */


#ifndef __INDEX_ARRAY_H__
#define __INDEX_ARRAY_H__ 

#include <ds/list.h>
#include <mstring/types.h>
#include <arch/bits.h>
#include <arch/page.h>

#define IA_MAX_VALUE  (~(range_type_t)0) 
#define IA_INVALID_VALUE  IA_MAX_VALUE

#define IA_ITEM_GRANULARITY  64  /* Bits */
#define IA_BITMAP_INIT_PATTERN  (~(uint64_t)0)

#define IA_ENTRY_SHIFT  9 /* 512 values */

#define IA_ENTRY_RANGE  (IA_ITEM_GRANULARITY*8) /* 512 values per entry */
#define IA_SIZE_MASK  (PAGE_SIZE-1)
#define IA_ENTRIES_PER_PAGE  (PAGE_SIZE / IA_ENTRY_RANGE)
#define IA_MAXRANGE  (IA_MAX_VALUE-IA_ENTRY_RANGE-1)

typedef uint32_t range_type_t;

typedef struct __index_array_entry {
  range_type_t id,values_left,range_start;
  uint64_t *bitmap;
  list_node_t l;
} index_array_entry_t;

typedef struct __index_array {
  range_type_t values_left, max_value;
  index_array_entry_t *entries;
  list_head_t free_entries,full_entries;
} index_array_t;

/**
 * @fn index_array_initialize(index_array_t *array, range_type_t num_entries)
 * @brief Initializes target index array object.
 *
 * This function initializes target index array so that the first entry retrieval
 * will give '0' as the first available entry.
 * If no memory allocation errors take place and a valid array size is passed,
 * this function returns 1. Otherwise, 0 is returned.
 *
 * @param array target index array to be initialized.
 * @param num_entries number of entries in this array. So the range for target
 * array will be [0 .. @a num_entries - 1].
 * Note ! @a num_entries must be aligned at page boundary !
 */
bool index_array_initialize(index_array_t *array, range_type_t num_entries);

/**
 * @fn bool index_array_deinitialize(index_array_t *array) 
 * @brief Deinitializes target index array.
 *
 * This function frees all memory occupied by target index array and makes it
 * unable for further elements retrievals/deallocations.
 */
void index_array_deinitialize(index_array_t *array);

/**
 * @fn index_array_alloc_value(index_array_t *array)
 * Retrieves the next available element from target index array.
 *
 * This function retrieves the next available element from target index
 * array and marks this element as 'unavailable', which means that untill
 * this element is freed it can't be retrieved once again.
 * If target array has free elements, the first available element is returned.
 * Otherwise, @a IA_INVALID_VALUE is returned.
 * Note: elements are allocated in incremental order which means that even if
 * you have freed element N this function might return N+k'th element.
 *
 * @param array target index array.
 */
range_type_t index_array_alloc_value(index_array_t *array);

/**
 * @fn bool index_array_free_value(index_array_t *array,range_type_t value)
 * @brief Makes target element available for target index array.
 *
 * This function makes target element available for target index array, which
 * means that it can be retrieved during further element retrievals.
 * If valid element is passed and target index array is initialized, this
 * funtion returns 1. Otherwise, 0 is returned.
 * Note: if an attempt is made to free entry that hasn't been allocated,
 * no elements are freed.
 */
bool index_array_free_value(index_array_t *array,range_type_t value); 

#endif
