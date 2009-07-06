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
#include <arch/mem.h>
#include <ds/list.h>
#include <mm/mmpool.h>
#include <mm/page.h>
#include <mstring/panic.h>
#include <mstring/assert.h>
#include <mstring/string.h>
#include <mstring/types.h>

mmpool_t *mmpools[ARCH_NUM_MMPOOLS];
mmpool_t *preferred_mmpools[NUM_PREFERRED_MMPOOLS];
LIST_DEFINE(mmpools_list);
static INITDATA mmpool_type_t mmpool_ids = MMPOOL_FIRST_TYPE;

static void mmpool_add_page(mmpool_t *mmpool, page_frame_t *pframe)
{
  ASSERT(pframe_number(pframe) >= mmpool->first_pidx);
  mmpool->num_pages++;
  if (pframe->flags & PF_RESERVED) {
    mmpool->num_reserved_pages++;
  }
  else {
    atomic_inc(&mmpool->num_free_pages);
  }

  pframe->flags |= mmpool->type;
}

INITCODE void mmpool_register(mmpool_t *mmpool)
{
  mmpool_type_t type;
  mmpool_t *p;
  list_node_t *n, *ins;
  
  ASSERT(mmpool != NULL);
  ins = list_node_last(&mmpools_list);
  list_for_each(&mmpools_list, n) {
    p = list_entry(n, mmpool_t, pool_node);
    if (p->bound_addr == mmpool->bound_addr) {
      panic("Memory pools \"%s\" and \"%s\" has the same "
            "bound address = %p!\n", mmpool->bound_addr);
    }
    if (mmpool->bound_addr < p->bound_addr) {
      ins = &p->pool_node;
      break;
    }
  }
  
  type = mmpool_ids++;
  ASSERT(type < MMPOOLS_MAX);
  mmpool->type = type;
  mmpools[type - 1] = mmpool;
  list_add_before(ins, &mmpool->pool_node);
}

INITCODE void mmpool_set_preferred(int mmpool_id, mmpool_t *pref_mmpool)
{
  ASSERT(pref_mmpool != NULL);
  ASSERT((mmpool_id >= 0) && (mmpool_id <= LAST_PREF_MMPOOL));
  ASSERT(preferred_mmpools[mmpool_id] == NULL);
  ASSERT(pref_mmpool->flags & (1 << mmpool_id));

  preferred_mmpools[mmpool_id] = pref_mmpool;
}

void mmpools_register_page(page_frame_t *pframe)
{
  uintptr_t addr = (uintptr_t)pframe_to_phys(pframe);
  bool added = false;
  mmpool_t *p;

  for_each_mmpool(p) {
    if (addr < p->bound_addr) {
      mmpool_add_page(p, pframe);
      added = true;
      break;
    }
  }
  if (unlikely(!added)) {
    panic("Can not determine memory pool for page with "
          "physical address %p!\n", addr);
  }
}
