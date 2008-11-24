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
 * (c) Copyright 2006,2007,2008 MString Core Team <http://mstring.berlios.de>
 * (c) Copyright 2008 Dan Kruchinin <dan.kruchinin@gmail.com>
 *
 * include/eza/mutex.h: mutex constants, API and defenitions.
 */


#ifndef __MUTEX_H__
#define __MUTEX_H__

#include <ds/waitqueue.h>
#include <eza/spinlock.h>
#include <eza/task.h>
#include <eza/arch/types.h>

/**
 * @brief General mutex structure
 *
 */
typedef struct __mutex {
  spinlock_t lock;
  wqueue_t wq;
  struct {
    task_t *task;
    uint32_t priority;
  } executer;
  uint32_t max_prio;
} mutex_t;

static inline bool mutex_is_locked(mutex_t *mutex)
{
  return !!mutex->executer.task;
}

void mutex_initialize(mutex_t *mutex);
void mutex_lock(mutex_t *mutex);
void mutex_unlock(mutex_t *mutex);
bool mutex_trylock(mutex_t *mutex);
bool mutex_is_locked(mutex_t *mutex);

#endif /* __MUTEX_H__ */
