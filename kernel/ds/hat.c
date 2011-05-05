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
 * ds/hat.c - Hashed array tree(HAT) implementation.
 * For more information bout HAT see:
 * "HATs: Hashed array trees", Dr. Dobb's Journal by Sitarski, Edward (September 1996)
 *
 */

#include <config.h>
#include <mm/slab.h>
#include <mstring/errno.h>
#include <mstring/stddef.h>
#include <mstring/types.h>
#include <ds/hat.h>

static memcache_t * leaves_cache = NULL;
static memcache_t * slots_cache = NULL;

/* Index computation in HAT's top */
static inline int get_top_index (hat_t *hat, ulong_t idx)
{
  return (idx) >> hat->power;
}

/* Index computation in leaf */
static inline int get_leaf_index(hat_t *hat, ulong_t idx)
{
  return (idx) & hat->leaf_mask;
}

/* Computation of HAT's maximum size with the current power of 2 */
static inline uint_t get_hat_max_size(hat_t * hat)
{
  return (1 << (hat->power * 2));
}

/* HAT leaf allocation */
static inline hat_leaf_t *create_hat_leaf(uint_t leaf_size)
{
  hat_leaf_t * ret_hl = alloc_from_memcache(leaves_cache, 0);
  if (!ret_hl)
    return NULL;
  memset(ret_hl, 0, sizeof(*ret_hl));
  ret_hl->slots = alloc_from_memcache(leaves_cache, 0);
  if (!ret_hl->slots)
    return NULL;
  return ret_hl;
}

/* HAT leaf deallocation */
static inline void __delete_hat_leaf(hat_leaf_t *leaf)
{
  if(leaf){
    if(leaf->slots)
      memfree(leaf->slots);
    memfree(leaf);
  }
}

/* HAT initialization */
int hat_initialize(hat_t *hat, ulong_t size)
{

  if(!hat)
    return -EINVAL;
  if (size > HAT_MAX_SIZE)
    return -EINVAL;

  /* Compute power of 2 needed for the given size */
  hat->power = bit_find_msf(size);

  if (!is_powerof2(size))
  hat->power++;

  if (!leaves_cache) {
    leaves_cache = create_memcache("HAT leaves", sizeof(hat_leaf_t), 1,
                                    MMPOOL_KERN | SMCF_IMMORTAL | SMCF_LAZY);
  if (!leaves_cache) {
      panic("Can not create buckets memory cache for HAT leaves! (failed to allocate %zd bytes)",
            sizeof(leaves_cache));
    }
  }

  if (!slots_cache) {
    slots_cache = create_memcache("HAT slots", sizeof(void *) * (1 << hat->power), 1,
                                    MMPOOL_KERN | SMCF_IMMORTAL | SMCF_LAZY);
  if (!slots_cache) {
      panic("Can not create buckets memory cache for HAT slots ! (failed to allocate %zd bytes)",
            sizeof(void *) * (1 << hat->power));
    }
  }


  hat->leaf_mask = (1 << hat->power) - 1;
  hat->size = size;
  hat->num_items = 0;
  hat->top = NULL;
  return 0;
}

/* Insert item in the hat at idx position */
int hat_insert(hat_t *hat, ulong_t idx, void *item)
{
  int leaf_id, top_id;
  hat_leaf_t * leaf;
  if (!hat)
    return -EINVAL;
  /* Check if idx is valid */
  if (idx > get_hat_max_size(hat))
    return -EINVAL;

  /* If top doesn't exist, we must create it */
  if (!hat->top)
  {
    leaf = create_hat_leaf((1 << hat->power));
    if (!leaf)
      return -ENOMEM;
    hat->top = leaf;
  }

  top_id = get_top_index(hat, idx);
  leaf = hat->top->slots[top_id];
  /* If the leaf at given position doesn't exist, we must create it */
  if (!leaf)
  {
    leaf = create_hat_leaf((1 << hat->power));
    if (!leaf)
      return -ENOMEM;
    hat->top->slots[top_id] = leaf;
  }
  leaf_id = get_leaf_index(hat, idx);
  leaf->slots[leaf_id] = item;
  leaf->num_items++;
  hat->num_items++;
  return 0;
}

/* Clear all items in the HAT, delete all leaves */
void hat_clear(hat_t *hat)
{
  int i;
  if ( (!hat) || (!hat->top))
    return;

  for (i = 0; i < (1<<hat->power); i++){
    if(hat->top->slots[i])
      __delete_hat_leaf(hat->top->slots[i]);
  }
  hat->num_items = 0;
}

/* Search item in the hat by index */
void* hat_lookup(hat_t *hat, ulong_t idx)
{
  hat_leaf_t *leaf;
  int leaf_id, top_id;

  if ( (!hat) || (!hat->top) || (idx > get_hat_max_size(hat)) )
    return NULL;

  top_id = get_top_index(hat, idx);
  leaf = hat->top->slots[top_id];
  if (!leaf)
    return NULL;

  leaf_id = get_leaf_index(hat, idx);
  return leaf->slots[leaf_id];
}

/* Delete item from HAT */
void hat_delete(hat_t *hat, ulong_t idx)
{
  hat_leaf_t *leaf;
  int leaf_id, top_id;
  if ( (!hat) || (!hat->top) || (!hat->num_items) || (idx > get_hat_max_size(hat)) )
    return;

  top_id = get_top_index(hat, idx);
  leaf = hat->top->slots[top_id];
  if (!leaf)
    return;

  leaf_id = get_leaf_index(hat, idx);

  leaf->slots[leaf_id] = NULL;
  leaf->num_items--;
  hat->num_items--;
  if(!leaf->num_items){
    /* there is no items in the slots,
      so we can delete this leaf */
    __delete_hat_leaf(leaf);
  }
}

/* Destroy hat_t structure */
void hat_destroy(hat_t *hat)
{
  if (!hat)
    return;
  hat_clear(hat);
  __delete_hat_leaf(hat->top);
  destroy_memcache(leaves_cache);
  destroy_memcache(slots_cache);
}
