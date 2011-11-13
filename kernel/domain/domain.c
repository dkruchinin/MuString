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
 * (c) Copyright 2010,2011 Jari OS non-profit org. <http://jarios.org>
 * (c) Copyright 2010,2011 Madtirra <madtirra@jarios.org>
 *
 * kernel/domain/domain.c: domains support
 */

#include <mstring/types.h>
#include <mstring/kstack.h>
#include <mstring/wait.h>
#include <arch/context.h>
#include <arch/current.h>
#include <mstring/limits.h>
#include <mstring/sigqueue.h>
#include <mstring/usercopy.h>
#include <sync/spinlock.h>
#include <arch/atomic.h>
#include <security/security.h>
#include <mstring/domain.h>
#include <mm/slab.h>
#include <config.h>

static memcache_t *ns_cache = NULL;
static memcache_t *ns_attrs_cache = NULL;

struct domain *root_domain = NULL;

static atomic_t ns_count = 0;

#define PANIC_PRE  "\n\tinitialize_domain_subsys: "

void initialize_domain_subsys(void)
{
  task_limits_t *def_limits = NULL;

  kprintf("[NS] Init domains subsystem ... ");

  ns_cache = create_memcache("Domain objects cache", sizeof(struct domain),
                             1, MMPOOL_KERN | SMCF_IMMORTAL | SMCF_LAZY);
  ns_attrs_cache = create_memcache("Domain attr objects cache", sizeof(struct dm_id_attrs),
                             1, MMPOOL_KERN | SMCF_IMMORTAL | SMCF_LAZY);

  if(!ns_cache || !ns_attrs_cache)
    panic(PANIC_PRE"Failed to initialize memory caches for domain objects.\n");

  /* create root domain */
  if(!(root_domain = alloc_domain_struct()))
    panic(PANIC_PRE"Failed to allocate root domain.\n");

  /* init default namespace */
  root_domain->dm_holder = DEFAULT_DOMAIN_HOLDER_PID;
  memcpy(root_domain->name, DEFAULT_DOMAIN_NAME, strlen(DEFAULT_DOMAIN_NAME));

  if(!(def_limits = allocate_task_limits()))
    panic(PANIC_PRE"Failed to allocate root domain default limits.\n");
  set_default_task_limits(def_limits);
  root_domain->def_limits = def_limits;
  root_domain->domain_pid_limit = CONFIG_MAX_PID_NUMBER;
  kprintf("OK\n");

  return;
}

struct domain *get_root_domain(void)
{
  return root_domain;
}

struct domain *alloc_domain_struct(void)
{
  struct domain *ns = NULL;

  if((ns = alloc_from_memcache(ns_cache, 0))) {
    atomic_set(&ns->use_count, 1);
    rw_spinlock_initialize(&ns->rw_lock, "DOMAIN lock");
    spinlock_initialize(&ns->pid_array_lock, "DOMAIN PID array");
    if ( idx_allocator_init(&ns->pid_array, CONFIG_MAX_PID_NUMBER) < 0 )
      return NULL;
    memset(ns->name, 0, 16);
    ns->dm_mm_limit = 0;
    ns->dm_holder = 0;
    ns->dm_id = 0;
    ns->pid_count = 0;
    ns->def_limits = NULL; /* should be initialized later */
  }

  return ns;
}

struct dm_id_attrs *alloc_dm_attrs(struct domain *ns)
{
  struct dm_id_attrs *ia = NULL;

  if((ia = alloc_from_memcache(ns_attrs_cache, 0))) {
    atomic_inc(&ns->use_count);
    ia->domain = ns;
    ia->dm_id = ns->dm_id;
    ia->trans_flag = 0;
  }

  return ia;
}

void destroy_dm_attrs(struct dm_id_attrs *id)
{
  atomic_dec(&id->domain->use_count);
  memfree(id);

  return;
}

void destroy_domain(struct domain *ns)
{
  memfree(ns);

  return;
}

/* top level functions */
int sys_chg_create_domain(ulong_t ns_mm_limit, ulong_t ns_pid_limit, char *short_name)
{
#ifndef CONFIG_ENABLE_DOMAIN
  return ERR(-ENOSYS);
#else
  int r = 0;
  struct domain *ns = NULL;
  task_t *task = current_task();
  task_limits_t *def_limits = NULL;

  /* check for the creator */
  if(task->domain->dm_id > 1) /* allowed only from root namespace */
    return ERR(-EPERM);

  /* create namespace */
  if(!(ns = alloc_domain_struct()))
    return ERR(-ENOMEM);

  /* check PID limit */
  if (ns_pid_limit > CONFIG_MAX_PID_NUMBER)
    return ERR(-EINVAL);

  /* init pid allocator */
  r = idx_allocator_init(&ns->pid_array, ns_pid_limit);
  if (r < 0) {
    return ERR(-ENOMEM);
  }
  ns->domain_pid_limit = ns_pid_limit;
  /* init default namespace */
  ns->dm_holder = task->pid;
  if(copy_from_user(ns->name, short_name, 16))
    return ERR(-EFAULT);
  ns->dm_mm_limit = ns_mm_limit;

  /* firstly assign default limits */
  if(!(def_limits = allocate_task_limits())) {
    destroy_domain(ns);
    return ERR(-ENOMEM);
  }
  set_default_task_limits(def_limits);
  ns->def_limits = def_limits;

  /* assign id */
  atomic_inc(&ns_count);
  ns->dm_id = (uint8_t)ns_count;

  /* change namespace attrs */
  task->domain->dm_id = ns->dm_id;
  task->domain->trans_flag = 1; /* carrier allowed to translate */
  task->domain->domain = ns;

  return ERR(r);
#endif
}

int sys_control_domain(pid_t task, int op_code, void *data)
{
  int r = 0;

  switch(op_code) {
  case DOMAIN_CTRL_GET_HOLDER_PID: /* all allowed */
    if(copy_to_user(data, &current_task()->domain->domain->dm_holder,
                    sizeof(pid_t)))
      r = -EFAULT;
    break;
  case DOMAIN_CTRL_REMOVE_TRANS: /* all allowed */
    current_task()->domain->trans_flag = 0;
    break;
  default:
    r = -EINVAL;
    break;
  }

  return ERR(r);
}
