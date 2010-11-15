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
 * (c) Copyright 2007-2010 Jari OS non-profit org. <http://jarios.org>
 *
 */

#ifndef __MSTRING_LIMITS_H__
#define  __MSTRING_LIMITS_H__

#include <sync/spinlock.h>
#include <arch/atomic.h>

#define LIMIT_UNLIMITED ~0UL

#define LIMIT_IPC_MAX_PORTS  0
#define LIMIT_IPC_MAX_PORT_MESSAGES  1
#define LIMIT_IPC_MAX_CHANNELS 2

#define LIMIT_NUM_LIMITS 3

typedef struct __task_limits {
  spinlock_t lock;
  atomic_t use_count;
  ulong_t limits[LIMIT_NUM_LIMITS];
} task_limits_t;

void initialize_limits(void);
void destroy_task_limits(task_limits_t *tl);
task_limits_t *allocate_task_limits(void);
void set_default_task_limits(task_limits_t *l);

static inline void get_task_limits(task_limits_t *l)
{
  atomic_inc(&l->use_count);
}

static inline void release_task_limits(task_limits_t *l)
{
  atomic_dec(&l->use_count);
}


#endif
