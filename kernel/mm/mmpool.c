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
 * (c) Copyright 2008 Dan Kruchinin <dan.kruchinin@gmail.com>
 *
 */

#include <config.h>
#include <arch/atomic.h>
#include <arch/mmpool_conf.h>
#include <mm/mmpool.h>
#include <mm/page.h>
#include <mstring/panic.h>
#include <mstring/assert.h>
#include <mstring/string.h>
#include <mstring/types.h>

mmpool_t *mmpools[ARCH_NUM_MMPOOLS];
mmpool_t *preferred_mmpools[NUM_PREFERRED_MMPOOLS];
static INITDATA SPINLOCK_DEFINE(mmpool_ids_lock);
static INITDATA mmpool_type_t mmpool_ids = MMPOOL_FIRST_TYPE;

INITCODE mmpool_type_t mmpool_register(mmpool_t *mmpool)
{
  mmpool_type_t type;
  
  ASSERT(mmpool != NULL);
  
  spinlock_lock(&mmpool_ids_lock);
  type = mmpool_ids++;
  spinlock_unlock(&mmpool_ids_lock);

  ASSERT(type < MMPOOLS_MAX);
  mmpool->type = type;
  mmpools[type - 1] = mmpool;
  return type;
}

INITCODE void mmpool_set_preferred(int mmpool_id, mmpool_t *pref_mmpool)
{
  ASSERT(pref_mmpool != NULL);
  ASSERT((mmpool_id >= 0) && (mmpool_id <= LAST_PREF_MMPOOL));
  ASSERT(preferred_mmpools[mmpool_id] == NULL);
  ASSERT(pref_mmpool->flags & (1 << mmpool_id));

  preferred_mmpools[mmpool_id] = pref_mmpool;
}

void mmpool_add_page(mmpool_t *mmpool, page_frame_t *pframe)
{
  if (pframe_number(pframe) < mmpool->first_pidx) {
    mmpool->first_pidx = pframe_number(pframe);
  }

  mmpool->num_pages++;
  if (pframe->flags & PF_RESERVED) {
    mmpool->num_reserved_pages++;
  }
  else {
    atomic_inc(&mmpool->num_free_pages);
  }

  pframe->flags |= mmpool->type;
}
