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
 * (c) Copyright 2009 Dan Kruchinin <dk@jarios.org>
 *
 * ds/hat.c - Hashed array tree(HAT) implementation.
 * For more information bout HAT see:
 * "HATs: Hashed array trees", Dr. Dobb's Journal by Sitarski, Edward (September 1996)
 *
 */

#include <config.h>
#include <mm/slab.h>
#include <ds/hat.h>
#include <mstring/errno.h>
#include <mstring/stddef.h>
#include <mstring/types.h>

static memcache_t *buckets_cache = NULL;

static inline int index2heigh(ulong_t idx)
{
  return (pow2(idx) / HAT_BUCKET_SHIFT);
}

static inline int index2slot_id(ulong_t idx, int heigh)
{
  return ((idx >> ((ulong_t)heigh * HAT_BUCKET_SHIFT)) & HAT_BUCKET_MASK);
}

static inline hat_bucket_t *create_hat_bucket(void)
{
    hat_bucket_t *ret_hb = alloc_from_memcache(buckets_cache, 0);
  if (!ret_hb)
    return NULL;

  memset(ret_hb, 0, sizeof(*ret_hb));
  return ret_hb;
}

static int expand_tree(hat_t *hat, int new_heigh)
{
  hat_bucket_t *hb;
  int h = hat->tree_heigh;

  while (h < new_heigh) {
    hb = create_hat_bucket();
    if (!hb)
      return -ENOMEM;  
    if (likely(hat->root_bucket != NULL)) {
      hb->slots[0] = hat->root_bucket;
      hb->num_items = 1;
    }

    hat->root_bucket = hb;
    h++;
  }
  
  hat->tree_heigh = new_heigh;
  return 0;
}

void hat_initialize(hat_t *hat)
{
  CT_ASSERT(is_powerof2(HAT_BUCKET_SLOTS));
  CT_ASSERT(sizeof(hat_bucket_t) <= SLAB_OBJECT_MAX_SIZE);
  if (!buckets_cache) {
    buckets_cache = create_memcache("HAT", sizeof(hat_bucket_t), 1,
                                    GENERAL_POOL_TYPE | SMCF_IMMORTAL | SMCF_LAZY);
    if (!buckets_cache) {
      panic("Can not create buckets memory cache for HAT! (failed to allocate %zd bytes)",
            sizeof(hat_bucket_t));
    }
  }
  
  hat->tree_heigh = -1;
  hat->root_bucket = NULL;
}

int hat_insert(hat_t *hat, ulong_t idx, void *item)
{
  int ret = 0;
  int heigh = index2heigh(idx), i;
  hat_bucket_t *hb, *parent_hb;

  ASSERT(heigh < HAT_HEIGH_MAX);
  if (unlikely(hat->tree_heigh < heigh)) {
    ret = expand_tree(hat, heigh);
    if (ret)
      return ret;
  }

  parent_hb = hat->root_bucket;
  while (heigh) {
    i = index2slot_id(idx, heigh);
    hb = parent_hb->slots[i];
    if (!hb) {
      hb = create_hat_bucket();
      if (!hb)
        return -ENOMEM;

      parent_hb->slots[i] = hb;
      parent_hb->num_items++;
    }

    parent_hb = hb;
    heigh--;
  }

  i = index2slot_id(idx, 0);
  if (parent_hb->slots[i] != NULL)
    return -EEXIST;

  parent_hb->slots[i] = item;
  parent_hb->num_items++;
  return ret;
}

void *hat_lookup(hat_t *hat, ulong_t idx)
{
  hat_bucket_t *hb;
  int h;

  if (index2heigh(idx) > hat->tree_heigh)
    return NULL;
  
  hb = hat->root_bucket;
  h = hat->tree_heigh;
  while (hb && (h > 0))
    hb = hb->slots[index2slot_id(idx, h--)];

  if (!hb)
    return NULL;

  return hb->slots[index2slot_id(idx, 0)];
}

void *hat_delete(hat_t *hat, ulong_t idx)
{
  void *path[HAT_HEIGH_MAX];
  hat_bucket_t *hb;
  void *ret;  
  int h, i;

  if (index2heigh(idx) > hat->tree_heigh)
    return NULL;

  h = hat->tree_heigh;
  hb = hat->root_bucket;
  while (h > 0) {
    path[h] = hb;
    i = index2slot_id(idx, h);
    hb = hb->slots[i];
    if (!hb)
      return NULL;
    
    h--;  
  }

  i = index2slot_id(idx, 0);
  ret = hb->slots[i];
  hb->slots[i] = NULL;
  h = 0;
  while (h <= hat->tree_heigh) {
    hb = path[h++];
    if (--hb->num_items > 0)
      break;

    memfree(hb);
  }
  if ((h >= hat->tree_heigh) && (hb == hat->root_bucket)) {
    hat->tree_heigh = -1;
    hat->root_bucket = NULL;
  }

  return ret;
}

