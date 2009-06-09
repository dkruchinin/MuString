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
 * include/sync/rwsem.h: Read-Write semaphore API definitions.
 */

#ifndef __RWSEM_H__
#define __RWSEM_H__

#include <config.h>
#include <mstring/waitqueue.h>
#include <sync/spinlock.h>
#include <mstring/types.h>

typedef struct __rwsem {
  int32_t stat;
  wqueue_t writers_wq;
  wqueue_t readers_wq;
  spinlock_t sem_lock;
} rwsem_t;

#define RWSEM_DEFINE(name)                      \
  rwsem_t (name) = RWSEM_INITIALIZE(name)

#define RWSEM_INITIALIZE(name)                  \
  { .stat = 0, .writers_wq = WQUEUE_INITIALIZE((name).writers_wq),  \
    .readers_wq = WQUEUE_INITIALIZE((name).readers_wq),             \
    .sem_lock = SPINLOCK_INITIALIZE(__SPINLOCK_UNLOCKED_V),  }

#define rwsem_is_locked(rs)       ((rs)->stat != 0)
#define rwsem_is_locked4read(rs)  ((rs)->stat > 0)
#define rwsem_is_locked4write(rs) ((rs)->stat < 0)

#define rwsem_down_read(rs)                     \
  __rwsem_down_read(rs, WQ_INSERT_SLEEP_UNR)
#define rwsem_down_write(rs)                    \
  __rwsem_down_write(rs, WQ_INSERT_SLEEP_UNR)
#define rwsem_down_read_intr(rs)                \
  __rwsem_down_read(rs, WQ_INSERT_SLEEN_INR)
#define rwsem_down_write_intr(rs)               \
  __rwsem_down_write(rs, WQ_INSERT_SLEEP_INR)
#define rwsem_try_down_read(rs)                 \
  __rwsem_try_down_read(rs, WQ_INSERT_SLEEP_UNR)
#define rwsem_try_down_write(rs)                \
  __rwsem_try_down_write(rs, WQ_INSERT_SLEEP_UNR)
#define rwsem_try_down_read_intr(rs)            \
  __rwsem_try_down_read(rs, WQ_INSERaT_SLEEP_INR)
#define rwsem_try_down_write_intr(rs)           \
  __rwsem_try_down_write(rs, WQ_INSERT_SLEEP_INR)

void rwsem_initialize(rwsem_t *rwsem);
void rwsem_up_read(rwsem_t *rwsem);
void rwsem_up_write(rwsem_t *rwsem);
void __rwsem_down_read(rwsem_t *rwsem, wqueue_insop_t iop);
void __rwsem_down_write(rwsem_t *rwsem, wqueue_insop_t iop);
bool __rwsem_try_down_read(rwsem_t *rwsem, wqueue_insop_t iop);
bool __rwsem_try_down_write(rwsem_t *rwsem, wqueue_insop_t iop);

#endif /* __RWSEM_H__ */
