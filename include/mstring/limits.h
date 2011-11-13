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
 * (c) Copyright 2011 Alex Firago <melg@jarios.org>
 */

#ifndef __MSTRING_LIMITS_H__
#define __MSTRING_LIMITS_H__

#include <sync/rwsem.h>
#include <arch/atomic.h>

#define LIMIT_UNLIMITED ~0UL

#define LIMIT_PORTS         0
#define LIMIT_PORT_MESSAGES 1
#define LIMIT_CHANNELS      2
#define LIMIT_TIMERS        3
#define LIMIT_TRHEADS       4
#define LIMIT_MEMORY        5

#define LIMIT_NUM_LIMITS    6



typedef struct __task_limits {
  rwsem_t lock;
  atomic_t use_count;
  ulong_t current_limits[LIMIT_NUM_LIMITS];
  ulong_t limits[LIMIT_NUM_LIMITS];
} task_limits_t;

void initialize_limits(void);
void destroy_task_limits(task_limits_t *tl);
task_limits_t *allocate_task_limits(void);
void set_default_task_limits(task_limits_t *l);
void set_limit(task_limits_t *l, uint_t id, ulong_t limit);
ulong_t get_limit(task_limits_t *l, uint_t id);

static inline task_limits_t * get_task_limits(task_limits_t *l)
{
  task_limits_t  * ret;
  atomic_inc(&l->use_count);
  rwsem_down_read(&l->lock);
  ret = l;
  rwsem_up_read(&l->lock);
  return l;
}

static inline void release_task_limits(task_limits_t *l)
{
  atomic_dec(&l->use_count);
}

/**
 * @fn long sys_get_limit(uint8_t ns_id, pid_t pid, uint_t index,void *data)
 * @brief Reads limit specified by index, namespace id and pid.
 *
 * This syscall reads limit specified by namespace id, task pid and index.
 * If namespace id or task pid is not specified, syscall returns limit of
 * current task.
 *
 * @param ns_id - Namespace id
 * @param pid   - Task pid.
 * @param index - Limit index(LIMIT_PORTS, LIMIT_CHANNELS...)
 * @return Limit on success or -ENOENT if ns_id, pid or index are invalid.
 */
long sys_get_limit(uint8_t ns_id, pid_t pid, uint_t index);


/**
 * @fn int sys_set_limit(uint8_t ns_id, pid_t pid, uint_t index, )
 * @brief Reads limit specified by index, namespace id and pid.
 *
 * This syscall updates limit specified by namespace id, task pid and index.
 * If current task is not namespace carrier syscall returns -EPERM.
 *
 * @param ns_id - Namespace id
 * @param pid   - Task pid.
 * @param index - Limit index(LIMIT_PORTS, LIMIT_CHANNELS...)
 * @return 0 on success ,-EINVAL if ns_id, pid or index are invalid.
 * or -EPERM if current task is not namespace carrier
 */
int sys_set_limit(uint8_t ns_id, pid_t pid, uint_t index, ulong_t limit);
#endif
