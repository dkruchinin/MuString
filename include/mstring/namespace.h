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
 * include/mstring/namespace.h: namespace support
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

#define DEFAULT_NS_CARRIER_PID  1

#define DEFAULT_NS_NAME  "Root NS"

struct namespace {
  uint8_t ns_id;  /* id of the namespace */
  ulong_t ns_mm_limit; /* pages per namespace limit */
  pid_t ns_carrier; /* user space namespace carrier */
  task_limits_t *def_limits; /* default limits for the namespace */
  atomic_t use_count;
  rw_spinlock_t rw_lock; /* rw lock */
  char name[16]; /* namespace short name */
};

/* this structure used for task_t */
struct ns_id_attrs {
  uint8_t ns_id;  /* used to avoid lock */
  uint8_t trans_flag;  /* translator flags, TODO: extend it in future */
  struct namespace *ns;  /* namespace assigned */
};

void initialize_ns_subsys(void);

struct namespace *alloc_namespace(void);

void destroy_namespace(struct namespace *);

#endif
