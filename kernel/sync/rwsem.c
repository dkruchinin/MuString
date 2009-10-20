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
 * (c) Copyright 2009 Dan Kruchinin <dan.kruchinin@gmail.com>
 *
 * mstring/sync/rwsem.c: Read-Write semaphore implementation.
 */

#include <config.h>
#include <mstring/waitqueue.h>
#include <sync/spinlock.h>
#include <sync/rwsem.h>
#include <mstring/types.h>

static always_inline void __rwsem_down_read_core(rwsem_t *rwsem, wqueue_insop_t iop)
{  
  if (rwsem_is_locked4write(rwsem)) {
    wqueue_task_t wq_task;

    waitqueue_prepare_task(&wq_task, current_task());
    waitqueue_insert_core(&rwsem->readers_wq, &wq_task, iop);
  }
  else
    rwsem->stat++;  
}

static always_inline void __rwsem_down_write_core(rwsem_t *rwsem, wqueue_insop_t iop)
{
  if (rwsem_is_locked(rwsem)) {
    wqueue_task_t wq_task;
    int ret;

    if (rwsem->stat < 0)
      rwsem->stat--;

    waitqueue_prepare_task(&wq_task, current_task());
    ret = waitqueue_insert_core(&rwsem->writers_wq, &wq_task, iop);
    if (unlikely((ret == -EINTR) && (rwsem->stat < 0)))
      rwsem->stat++;
  }
  else
    rwsem->stat--;
}

void rwsem_initialize(rwsem_t *rwsem)
{
  rwsem->stat = 0;
  waitqueue_initialize(&rwsem->writers_wq);
  waitqueue_initialize(&rwsem->readers_wq);
  spinlock_initialize(&rwsem->sem_lock, "R/W semaphore");
}

void __rwsem_down_write(rwsem_t *rwsem, wqueue_insop_t iop)
{
  spinlock_lock(&rwsem->sem_lock);
  __rwsem_down_write_core(rwsem, iop);
  spinlock_unlock(&rwsem->sem_lock);
}

void __rwsem_down_read(rwsem_t *rwsem, wqueue_insop_t iop)
{
  spinlock_lock(&rwsem->sem_lock);
  __rwsem_down_read_core(rwsem, iop);
  spinlock_unlock(&rwsem->sem_lock);
}

void rwsem_up_write(rwsem_t *rwsem)
{
  spinlock_lock(&rwsem->sem_lock);
  ASSERT(rwsem->stat < 0);
  if (++rwsem->stat < 0)
    waitqueue_pop(&rwsem->writers_wq, NULL);
  else if (!waitqueue_is_empty(&rwsem->readers_wq)) {
    int waiters = rwsem->readers_wq.num_waiters;
    
    rwsem->stat += waiters;
    while (waiters--)
      waitqueue_pop(&rwsem->readers_wq, NULL);
  }

  spinlock_unlock(&rwsem->sem_lock);
}

void rwsem_up_read(rwsem_t *rwsem)
{
  spinlock_lock(&rwsem->sem_lock);
  ASSERT(rwsem->stat > 0);
  rwsem->stat--;
  if (!rwsem->stat && !waitqueue_is_empty(&rwsem->writers_wq)) {
    int ret;
    
    rwsem->stat -= rwsem->writers_wq.num_waiters;
    ret = waitqueue_pop(&rwsem->writers_wq, NULL);
    ASSERT(ret == 0);
  }
  
  spinlock_unlock(&rwsem->sem_lock);  
}

bool __rwsem_try_down_read(rwsem_t *rwsem, wqueue_insop_t iop)
{
  bool rt = false;
  
  if (rwsem_is_locked4write(rwsem))
    return rt;

  spinlock_lock(&rwsem->sem_lock);
  if (likely(!rwsem_is_locked4write(rwsem))) {
    __rwsem_down_read_core(rwsem, iop);
    rt = true;
  }

  spinlock_unlock(&rwsem->sem_lock);
  return rt;
}

bool __rwsem_try_down_write(rwsem_t *rwsem, wqueue_insop_t iop)
{
  bool rt = false;

  if (rwsem_is_locked(rwsem))
    return rt;

  spinlock_lock(&rwsem->sem_lock);
  if (likely(!rwsem_is_locked(rwsem))) {
    __rwsem_down_write_core(rwsem, iop);
    rt = true;
  }
  
  spinlock_unlock(&rwsem->sem_lock);
  return rt;
}

