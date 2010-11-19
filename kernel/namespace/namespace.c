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
 * (c) Copyright 2010 Jari OS non-profit org. <http://jarios.org>
 * (c) Copyright 2010 Madtirra <madtirra@jarios.org>
 *
 * kernel/namespace.c: namespace support
 */

#include <mstring/types.h>
#include <mstring/kstack.h>
#include <mstring/wait.h>
#include <arch/context.h>
#include <arch/current.h>
#include <mstring/limits.h>
#include <mstring/sigqueue.h>
#include <sync/spinlock.h>
#include <arch/atomic.h>
#include <security/security.h>
#include <mstring/namespace.h>
#include <mm/slab.h>

#if 0
#define DEFAULT_NS_NAME  "Root NS"

/* this structure used for task_t */
struct ns_id_attrs {
  uint8_t ns_id;  /* used to avoid lock */
  uint8_t trans_flag;  /* translator flags, TODO: extend it in future */
  struct namespace *ns;  /* namespace assigned */
};

#endif

static memcache_t *ns_cache = NULL;
static memcache_t *ns_attrs_cache = NULL;

static struct namespace *root_ns = NULL;

#define PANIC_PRE  "\n\tinitialize_ns_subsys: "

void initialize_ns_subsys(void)
{
  task_limits_t *def_limits = NULL;

  kprintf("[NS] Init namespace subsystem ... ");

  ns_cache = create_memcache("NS objects cache", sizeof(struct namespace),
                             1, MMPOOL_KERN | SMCF_IMMORTAL | SMCF_LAZY);
  ns_attrs_cache = create_memcache("NS attr objects cache", sizeof(struct ns_id_attrs),
                             1, MMPOOL_KERN | SMCF_IMMORTAL | SMCF_LAZY);

  if(!ns_cache || !ns_attrs_cache)
    panic(PANIC_PRE"Failed to initialize memory caches for namespace objects.\n");

  /* create root namespace */
  if(!(root_ns = alloc_namespace()))
    panic(PANIC_PRE"Failed to allocate root namespace.\n");

  /* init default namespace */
  root_ns->ns_carrier = DEFAULT_NS_CARRIER_PID;
  memcpy(root_ns->name, DEFAULT_NS_NAME, strlen(DEFAULT_NS_NAME));

  if(!(def_limits = allocate_task_limits()))
    panic(PANIC_PRE"Failed to allocate root namespace default limits.\n");
  set_default_task_limits(def_limits);
  root_ns->def_limits = def_limits;

  kprintf("OK\n");

  return;
}

struct namespace *alloc_namespace(void)
{
  struct namespace *ns = NULL;

  if((ns = alloc_from_memcache(ns_cache, 0))) {
    atomic_set(&ns->use_count, 1);
    rw_spinlock_initialize(&ns->rw_lock, "NS lock");

    memset(ns->name, 0, 16);
    ns->ns_mm_limit = 0;
    ns->ns_carrier = 0;
    ns->ns_id = 0;
    ns->def_limits = NULL; /* should be initialized later */
  }

  return ns;
}

void destroy_namespace(struct namespace *ns)
{
  return;
}
