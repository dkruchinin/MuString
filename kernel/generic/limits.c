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
 * (c) Copyright 2008 Michael Tsymbalyuk <mtzaurus@jarios.org>
 * (c) Copyright 2007,2010 Jari OS non-profit org. <http://jarios.org>
 *
 */

#include <arch/types.h>
#include <mstring/limits.h>
#include <mm/page_alloc.h>
#include <mm/page.h>
#include <mm/slab.h>
#include <ipc/ipc.h>
#include <sync/spinlock.h>
#include <config.h>

static memcache_t *limits_memcache = NULL;

void initialize_limits(void)
{
  kprintf("[KERN] Initializating limits ...\n");

  limits_memcache = create_memcache("Limit objects cache", sizeof(task_limits_t),
                                    1, MMPOOL_KERN | SMCF_IMMORTAL | SMCF_LAZY);
  if(!limits_memcache)
    panic("initialize_limits: Failed to initialize memory cache for limit objects.\n");

  return;
}

task_limits_t *allocate_task_limits(void)
{
  task_limits_t *tl = alloc_from_memcache(limits_memcache, 0);

  if( tl != NULL ) {
    atomic_set(&tl->use_count,1);
    spinlock_initialize(&tl->lock, "Task limits");
    return tl;
  } else
    return NULL;
}

void destroy_task_limits(task_limits_t *tl)
{
  memfree(tl);
  return;
}

void set_default_task_limits(task_limits_t *l)
{
  l->limits[LIMIT_IPC_MAX_PORTS]=CONFIG_IPC_DEFAULT_PORTS;
  l->limits[LIMIT_IPC_MAX_PORT_MESSAGES]=IPC_DEFAULT_PORT_MESSAGES;
  l->limits[LIMIT_IPC_MAX_CHANNELS]=CONFIG_IPC_DEFAULT_CHANNELS;
}
