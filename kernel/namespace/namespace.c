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
#include <mstring/usercopy.h>
#include <sync/spinlock.h>
#include <arch/atomic.h>
#include <security/security.h>
#include <mstring/namespace.h>
#include <mm/slab.h>
#include <config.h>

static memcache_t *ns_cache = NULL;
static memcache_t *ns_attrs_cache = NULL;

struct namespace *root_ns = NULL;

static atomic_t ns_count = 0;

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
  root_ns->ns_pid_limit = CONFIG_MAX_PID_NUMBER;
  kprintf("OK\n");

  return;
}

struct namespace *get_root_namespace(void)
{
  return root_ns;
}

struct namespace *alloc_namespace(void)
{
  struct namespace *ns = NULL;

  if((ns = alloc_from_memcache(ns_cache, 0))) {
    atomic_set(&ns->use_count, 1);
    rw_spinlock_initialize(&ns->rw_lock, "NS lock");
    spinlock_initialize(&ns->pid_array_lock, "NS PID array");
    if ( idx_allocator_init(&ns->pid_array, CONFIG_MAX_PID_NUMBER) < 0 )
      return NULL;
    memset(ns->name, 0, 16);
    ns->ns_mm_limit = 0;
    ns->ns_carrier = 0;
    ns->ns_id = 0;
    ns->pid_count = 0;
    ns->def_limits = NULL; /* should be initialized later */
  }

  return ns;
}

struct ns_id_attrs *alloc_ns_attrs(struct namespace *ns)
{
  struct ns_id_attrs *ia = NULL;

  if((ia = alloc_from_memcache(ns_attrs_cache, 0))) {
    atomic_inc(&ns->use_count);
    ia->ns = ns;
    ia->ns_id = ns->ns_id;
    ia->trans_flag = 0;
  }

  return ia;
}

void destroy_ns_attrs(struct ns_id_attrs *id)
{
  atomic_dec(&id->ns->use_count);
  memfree(id);

  return;
}

void destroy_namespace(struct namespace *ns)
{
  memfree(ns);

  return;
}

/* top level functions */
int sys_chg_create_namespace(ulong_t ns_mm_limit, ulong_t ns_pid_limit, char *short_name)
{
#ifndef CONFIG_ENABLE_NS
  return ERR(-ENOSYS);
#else
  int r = 0;
  struct namespace *ns = NULL;
  task_t *task=current_task();
  task_limits_t *def_limits = NULL;

  /* check for the creator */
  if(task->namespace->ns_id > 1) /* allowed only from root namespace */
    return ERR(-EPERM);

  /* create namespace */
  if(!(ns = alloc_namespace()))
    return ERR(-ENOMEM);

  /* check PID limit */
  if (ns_pid_limit > CONFIG_MAX_PID_NUMBER)
    return ERR(-EINVAL);

  /* init pid allocator */
  r = idx_allocator_init(&ns->pid_array, ns_pid_limit);
  if (r < 0) {
    return ERR(-ENOMEM);
  }
  ns->ns_pid_limit = ns_pid_limit;
  /* init default namespace */
  ns->ns_carrier = task->pid;
  if(copy_from_user(ns->name, short_name, 16))
    return ERR(-EFAULT);
  ns->ns_mm_limit = ns_mm_limit;

  /* firstly assign default limits */
  if(!(def_limits = allocate_task_limits())) {
    destroy_namespace(ns);
    return ERR(-ENOMEM);
  }
  set_default_task_limits(def_limits);
  ns->def_limits = def_limits;

  /* assign id */
  atomic_inc(&ns_count);
  ns->ns_id = (uint8_t)ns_count;

  /* change namespace attrs */
  task->namespace->ns_id = ns->ns_id;
  task->namespace->trans_flag = 1; /* carrier allowed to translate */
  task->namespace->ns = ns;

  return ERR(r);
#endif
}

int sys_control_namespace(pid_t task, int op_code, void *data)
{
  int r = 0;

  switch(op_code) {
  case NS_CTRL_GET_CARRIER_PID: /* all allowed */
    if(copy_to_user(data, &current_task()->namespace->ns->ns_carrier,
                    sizeof(pid_t)))
      r = -EFAULT;
    break;
  case NS_CTRL_REMOVE_TRANS: /* all allowed */
    current_task()->namespace->trans_flag = 0;
    break;
  default:
    r = -EINVAL;
    break;
  }

  return ERR(r);
}
