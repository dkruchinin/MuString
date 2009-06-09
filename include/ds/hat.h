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
 * include/ds/hat.h - Hashed array tree(HAT) API definitions.
 * For more information bout HAT see:
 * "HATs: Hashed array trees", Dr. Dobb's Journal by Sitarski, Edward (September 1996)
 *
 */

#ifndef __HAT_H__
#define __HAT_H__

#include <config.h>
#include <mstring/types.h>

#define HAT_BUCKET_SHIFT 6
#define HAT_BUCKET_SLOTS (1UL << HAT_BUCKET_SHIFT)
#define HAT_BUCKET_MASK  (HAT_BUCKET_SLOTS - 1)
#define HAT_HEIGH_MAX 31

typedef struct __hat_bucket {  
  void *slots[HAT_BUCKET_SLOTS];
  int num_items;
} hat_bucket_t;

typedef struct __hat {  
  hat_bucket_t *root_bucket;
  int tree_heigh;
} hat_t;

void hat_initialize(hat_t *hat);
int hat_insert(hat_t *hat, ulong_t idx, void *item);
void *hat_lookup(hat_t *hat, ulong_t idx);
void *hat_delete(hat_t *hat, ulong_t idx);

#endif /* __HAT_H__ */
