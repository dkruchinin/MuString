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
 * ds/linked_array.h: Prototypes for the linked array data type.
 *
 */

#ifndef __LINKED_ARRAY_H__
#define __LINKED_ARRAY_H__

#include <eza/arch/types.h>

#define INVALID_ITEM_IDX  ~(ulong_t)0

typedef struct __linked_array {
  ulong_t items,head,item_size;
  uint8_t *array;
} linked_array_t;

/**
 * @fn int linked_array_initialize(linked_array_t *arr,ulong_t items)
 * @brief Initialize target linked list.
 *
 * This function initializes target linked list, which means allocation
 * its data array, setting up its main fields and marking the array as
 * 'ready for use'. Linked lists must be initialized before use.
 * This function is also invokes 'linked_array_reset()' as a part of its
 * logic.
 *
 * @param arr - Target array.
 * @param items - Items in array.
 * @return If target array was successfully initialized, 0 is returned.
 * Otherwise, -1 is returned.
 */
int linked_array_initialize(linked_array_t *arr,ulong_t items);

/**
 * @fn int linked_array_deinitialize(linked_array_t *arr)
 * @brief Deinitialize target linked list.
 *
 * This function deinitializes target linked list, which means deallocation
 * its data array, and marking the array as 'not ready for use'.
 *
 * @param arr - Target array.
 */
void linked_array_deinitialize(linked_array_t *arr);

/**
 * @fn int linked_array_alloc_item(linked_array_t *arr)
 * @brief Allocate item from target linked list.
 *
 * This function gets the next available item from target linked array.
 *
 * @param arr - Target array.
 * @return If a free item was found, its index is returned. Otherwise,
 * 'INVALID_ITEM_IDX' is returned.
 */
ulong_t linked_array_alloc_item(linked_array_t *arr);

/**
 * @fn int linked_array_free_item(linked_array_t *arr,ulong_t item)
 * @brief Free item in target linked list.
 *
 * This function frees the given item in target linked array. This entry
 * will be available during the next entry allocation procedure.
 *
 * @param arr - Target array.
 * @param entry - Entry to be freed
 */
void linked_array_free_item(linked_array_t *arr,ulong_t item);

/**
 * @fn int linked_array_reset(linked_array_t *arr)
 * @brief Resets target linked list.
 *
 * This function resets target linked array which means that all
 * entries are available for future allocations and the first available
 * entry is entry number zero.
 *
 * @param arr - Target array.
 */
void linked_array_reset(linked_array_t *arr);

#endif
