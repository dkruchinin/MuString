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
 * (c) Copyright 2009 Dan Kruchinin <dan.kruchinin@gmail.com>
 *
 * include/ds/idx_allocator.h: Index allocator API and definitions
 *
 */

/**
 * @file include/ds/idx_allocator.h
 * @author Dan Kruchinin
 * @brief Index allocator API and definitions
 *
 * Index allocator is a quite simple bitmaps based data
 * structure allowing to allocate non-negative integers
 * from an ordered continuous numbers set.
 * It may be useful for dynamic allocation of pids, uids and
 * other identifiers that must be unique.
 * Any identifier from a given set may be allocated, deallocated and reserved
 * (which prevents its allocation).
 * Note, there is no any sence to use index allocator for relatively
 * small(for example for sets containing less than 1024 items) identifier sets.
 * Allocator uses *at least* BYTES_PER_ITEM + sizeof(ulong_t) bytes for its internal bitmaps.
 */

#ifndef __IDX_ALLOCATOR_H__
#define __IDX_ALLOCATOR_H__

#include <mlibc/types.h>

#define BYTES_PER_ITEM 64 /**< Number of bytes per one entry of a second-level bitmap */
#define WORDS_PER_ITEM (BYTES_PER_ITEM / sizeof(ulong_t)) /**< Number of machine words(ulong_t) per second-level bitmap item */
#define IDX_INVAL    ~0UL /**< Invalid index value */

/**
 * @struct idx_allocator_t
 * @brief Index allocator structure
 */
typedef struct __idx_allocator {
  int size;            /**< Total number of words used for second-level bitmap */
  ulong_t max_id;      /**< Maximum index value(exclusive) */
  ulong_t *main_bmap;  /**< First-level(main) bitmap that splits second-level bitmap on several parts */
  ulong_t *ids_bmap;   /**< Second-level bitmap whose each bit corresponds to particular unique identifier */
  struct {
    ulong_t (*alloc_id)(struct __idx_allocator *ida);
    void (*reserve_id)(struct __idx_allocator *ida);
    void (*free_id)(struct __idx_allocator *ida);
  } ops;
} idx_allocator_t;

/**
 * @brief Initialize an index allocator.
 * @param ida     - A pointer to particular index allocator
 * @param idx_max - Maximum index value.
 */
int idx_allocator_init(idx_allocator_t *ida, ulong_t idx_max);

/**
 * @brief Destroy index allocator.
 *
 * This function frees space used by allocator's internal
 * bitmaps. After it is called, allocator can not be used.
 *
 * @param ida - A pointer to praticular index allocator.
 */
void idx_allocator_destroy(idx_allocator_t *ida);

/**
 * @brief Allocate new index from an allocator's set.
 * @param ida - A pointer to target index allocator.
 * @return Valid index value on success, IDX_INVAL on error.
 * @see IDX_INVAL
 * @see idx_reserve
 * @see idx_free
 */
ulong_t idx_allocate(idx_allocator_t *ida);

/**
 * @brief Reserves particular index number.
 *
 * This action prevents named index number from
 * allocation. Index allocator skips reserved indices until
 * they are fried with idx_free
 *
 * @param ida - A pointer to particular index allocator.
 * @param idx - Number of index to reserve.
 * @see idx_free
 * @see idx_allocate
 */
void idx_reserve(idx_allocator_t *ida, ulong_t idx);

/**
 * @brief Free back index @a idx to named allocator @a ida.
 * @param ida - Allocator index will be freed back to.
 * @param idx - An index to free.
 * @see idx_allocate
 * @see idx_reserve
 */
void idx_free(idx_allocator_t *ida, ulong_t idx);

#endif /* __IDX_ALLOCATOR_H__ */
