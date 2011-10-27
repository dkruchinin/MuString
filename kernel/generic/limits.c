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
 * (c) Copyright 2011 Alex Firago <melg@jarios.org>
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
static ulong_t __upper_limits[LIMIT_NUM_LIMITS];

void initialize_limits(void)
{
  kprintf("[KERN] Initializating limits ...\n");

  limits_memcache = create_memcache("Limit objects cache", sizeof(task_limits_t),
                                    1, MMPOOL_KERN | SMCF_IMMORTAL | SMCF_LAZY);
  if(!limits_memcache)
    panic("initialize_limits: Failed to initialize memory cache for limit objects.\n");
  __upper_limits[LIMIT_PORTS] = CONFIG_TASK_PORTS_LIMIT_MAX;
  __upper_limits[LIMIT_PORT_MESSAGES] = CONFIG_TASK_MESSAGES_LIMIT_MAX;
  __upper_limits[LIMIT_CHANNELS] = CONFIG_TASK_CHANNELS_LIMIT_MAX;
  __upper_limits[LIMIT_MEMORY]  = CONFIG_TASK_MEMORY_LIMIT_MAX;
  __upper_limits[LIMIT_TIMERS]  = CONFIG_TASK_TIMERS_LIMIT_MAX;
  __upper_limits[LIMIT_TRHEADS] = CONFIG_TASK_THREADS_LIMIT_MAX;
  return;
}

task_limits_t *allocate_task_limits(void)
{
  task_limits_t *tl = alloc_from_memcache(limits_memcache, 0);

  if( tl != NULL ) {
    atomic_set(&tl->use_count,1);
    rwsem_initialize(&tl->lock);
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
  rwsem_down_write(&l->lock);
  l->limits[LIMIT_PORTS] = CONFIG_TASK_PORTS_LIMIT;
  l->limits[LIMIT_PORT_MESSAGES] = CONFIG_TASK_MESSAGES_LIMIT;
  l->limits[LIMIT_CHANNELS] = CONFIG_TASK_CHANNELS_LIMIT;
  l->limits[LIMIT_MEMORY] = CONFIG_TASK_MEMORY_LIMIT;
  l->limits[LIMIT_TIMERS] = CONFIG_TASK_TIMERS_LIMIT;
  l->limits[LIMIT_TRHEADS] = CONFIG_TASK_THREADS_LIMIT;
  rwsem_up_write(&l->lock);
}

void set_limit(task_limits_t *l, uint_t id, ulong_t limit)
{
  if (id > LIMIT_NUM_LIMITS)
    return;
  rwsem_down_write(&l->lock);
  if (limit > __upper_limits[id])
    l->limits[id] = __upper_limits[id];
  else
    l->limits[id] = limit;
  rwsem_up_write(&l->lock);
}

ulong_t get_limit(task_limits_t *l, uint_t id)
{
  ulong_t lim;
  if (id > LIMIT_NUM_LIMITS)
    return 0;
  rwsem_down_read(&l->lock);
  lim = l->limits[id];
  rwsem_up_read(&l->lock);
  return lim;
}


/* Top level functions */
long sys_get_limit(uint8_t ns_id, pid_t pid, uint_t index)
{
  task_t *task = NULL;
  long r;
#ifndef CONFIG_ENABLE_DOMAIN
  if (pid==INVALID_PID)
    task = current_task();
  else
    task = lookup_task(pid, 0, 0);

  if(!task)
    return ERR(-ENOENT);

  if(index > LIMIT_NUM_LIMITS)
    return ERR(-ENOENT);

  r = get_limit(task->limits, index);
  return ERR(r);
#else
//TODO
#endif
}

int sys_set_limit(uint8_t ns_id, pid_t pid, uint_t index, ulong_t limit)
{
  task_t *task = NULL;
#ifndef CONFIG_ENABLE_DOMAIN
  task = current_task();
  if (!task->pid == DEFAULT_NS_CARRIER_PID)
    return ERR(-EPERM);

  task = lookup_task(pid, 0, 0);
  if(!task)
    return ERR(-ENOENT);

  if(index > LIMIT_NUM_LIMITS)
    return ERR(-ENOENT);

  set_limit(task->limits, index, limit);
  return ERR(0);
#else
  //TODO
#endif
}
