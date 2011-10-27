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
 * include/mstring/domain.h: domain support
 */


#ifndef __NAMESPACE_H__
#define __NAMESPACE_H__

#include <mstring/types.h>
#include <mstring/kstack.h>
#include <mstring/wait.h>
#include <arch/context.h>
#include <arch/current.h>
#include <mm/page.h>
#include <mm/vmm.h>
#include <mstring/limits.h>
#include <mstring/sigqueue.h>
#include <sync/mutex.h>
#include <mstring/event.h>
#include <mstring/scheduler.h>
#include <ds/idx_allocator.h>
#include <arch/atomic.h>
#include <security/security.h>

/* Maximum number of processes per namespace */
#define CONFIG_MAX_PID_NUMBER 32767

#define DEFAULT_DOMAIN_HOLDER_PID  1

#define DEFAULT_DOMAIN_NAME  "Root DOMAIN"

struct domain {
  uint8_t dm_id;              /* id of the domain */
  ulong_t dm_mm_limit;        /* pages per domain limit */
  pid_t dm_holder;            /* user space domain holder */
  task_limits_t *def_limits;  /* default limits for the domain */
  atomic_t use_count;
  rw_spinlock_t rw_lock;      /* rw lock */

  ulong_t domain_pid_limit;   /* processes per domain limit */
  idx_allocator_t pid_array;  /* Array of PIDs for the given domain */
  spinlock_t pid_array_lock;  /* Lock for PID array */
  ulong_t pid_count;          /* Current number of processes in the domain */
  char name[16];              /* domain short name */
};

/* this structure used for task_t */
struct dm_id_attrs {
  uint8_t dm_id;         /* used to avoid lock */
  uint8_t trans_flag;    /* translator flags, TODO: extend it in future */
  struct domain *domain; /* domain assigned */
};

/* Root namespace struct */
extern struct domain *root_domain;

void initialize_domain_subsys(void);

struct domain *alloc_domain_struct(void);

void destroy_domain(struct domain *);

struct domain *get_root_domain(void);

/* attribute structure ops */
struct dm_id_attrs *alloc_dm_attrs(struct domain *);
void destroy_dm_attrs(struct dm_id_attrs *);

/* top level wrapper (syscalls) */

/* control cmd */
#define DOMAIN_CTRL_GET_HOLDER_PID  0x0
#define DOMAIN_CTRL_REMOVE_TRANS     0x1

int sys_chg_create_domain(ulong_t, ulong_t, char *);
int sys_control_domain(pid_t, int, void *);

#endif
