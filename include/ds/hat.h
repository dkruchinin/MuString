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
 * (c) Copyright 2011 Alex Firago <melg@jarios.org>
 *
 * For more information bout HAT see:
 * "HATs: Hashed array trees", Dr. Dobb's Journal by Sitarski, Edward (September 1996)
 */

/**
 * @file include/ds/hat.h
 * For more information bout HAT see:
 * "HATs: Hashed array trees", Dr. Dobb's Journal by Sitarski, Edward (September 1996)
 * @author Alex Firago
 */
#ifndef _HAT_H_
#define _HAT_H_

#include <config.h>
#include <mstring/types.h>

#define HAT_DEFAULT_POWER 5
#define HAT_MAX_POWER 12
#define HAT_MAX_SIZE (1UL << HAT_MAX_POWER)
#define HAT_DEFAULT_SIZE (1UL << HAT_DEFAULT_POWER)

#define hat_is_empty(hat)  ((hat)->num_items == 0)

/**
 * @struct hat_leaf_t
 * Hashed array tree's universal abstract container
 */
typedef struct __hat_leaf {
  void **slots;  /**< Slots for pointers to abstract data*/
  int num_items; /**< Total number items in slots */
} hat_leaf_t;

/**
 * @struct hat_t
 * Hashed array tree
 */
typedef struct __hat {
  hat_leaf_t *top;   /**< Top leaf containing pointers to other leaves */
  ulong_t num_items; /**< Total number of items in the HAT */
  ulong_t power;      /**< Power of 2 used in this HAT */
  ulong_t size;       /**< Size of HAT */
  ulong_t leaf_mask; /**< Mask used for index calculation */
} hat_t;


/**
 * @brief Initialize @hat with @size.
 *
 * @param size - Number of bytes to allocate
 * @return 0 on success, -EINVAL if @hat is NULL or @size is greater than HAT_MAX_SIZE.
 * @note @hat will be initialized with nearest power of 2
 */
int hat_initialize(hat_t *hat, ulong_t size);

/**
 * @brief Insert @item in the @hat at @idx position.
 * @return 0 on success,
 *    -EINVAL if @hat is NULL or @idx is greater than current hat max size.
 *    -ENOMEM if memory allocation for leaves wasn't successfull
 */
int hat_insert(hat_t *hat, ulong_t idx, void *item);
void  hat_clear(hat_t *hat);

/**
 * @brief Locate item in the @hat by @idx index.
 * @return pointer to item on success, NULL otherwise.
 */
void* hat_lookup(hat_t *hat, ulong_t idx);
void  hat_delete(hat_t *hat, ulong_t idx);
void hat_destroy(hat_t *hat);
#endif
