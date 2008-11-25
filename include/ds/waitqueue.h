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
 * (c) Copyright 2008 Michael Tsymbalyuk <mtzaurus@gmail.com>
 * (c) Copyright 2008 Dan Kruchinin <dan.kruchinin@gmail.com>
 *
 * include/eza/waitqueue.h: prototypes for waitqueues.
 */

#ifndef __WAITQUEUE_H__
#define __WAITQUEUE_H__

#include <ds/list.h>
#include <eza/task.h>
#include <eza/spinlock.h>
#include <eza/errno.h>
#include <eza/arch/types.h>

typedef struct __wait_queue {
  list_head_t waiters;
  uint32_t num_waiters;
  spinlock_t q_lock;
} wait_queue_t;

typedef struct __wait_queue_task {
  list_head_t head;
  list_node_t node;
  task_t *task;
  wait_queue_t *q;
  uint32_t priority;
} wait_queue_task_t;

typedef enum __wqueue_insop {
  WQ_INSERT_SIMPLE = 0,
  WQ_INSERT_SLEEP,
} wqueue_insop_t;

typedef enum __wqeue_delop {
  WQ_DELETE_SIMPLE = 0,
  WQ_DELETE_WAKEUP,
} wqueue_delop_t;

#define WAITQUEUE_DEFINE(name)                          \
    wait_queue_t (name) = WAITQUEUE_INITIALIZE(name)

#define WAITQUEUE_INITIALIZE(name)                                      \
  {   .waiters = LIST_INITIALIZE((name).waiters),                       \
      .num_waiters = 0,                                                 \
      .q_lock = SPINLOCK_INITIALIZE(__SPINLOCK_UNLOCKED_V), }

#define waitqueue_push(wq, wq_task)                 \
  waitqueue_insert(wq, wq_task, WQ_INSERT_SLEEP);

#define waitqueue_pop(wq)                                           \
  waitqueue_delete(wq, waitqueue_first_task(wq), WQ_DELETE_WAKEUP);

static inline bool waitqueue_is_empty(wait_queue_t *wq)
{
  return !(wq->num_waiters);
}

status_t waitqueue_initialize(wait_queue_t *wq);
status_t waitqueue_prepare_task(wait_queue_task_t *wq_task, task_t *task);
status_t waitqueue_insert(wait_queue_t *wq, wait_queue_task_t *wq_task, wqueue_insop_t iop);
status_t waitqueue_delete(wait_queue_t *wq, wait_queue_task_t *wq_task, wqueue_delop_t dop);
wait_queue_task_t *waitqueue_first_task(wait_queue_t *wq);

#define DEBUG_WAITQUEUE /* FIXME DK: remove after debug */
#ifdef DEBUG_WAITQUEUE
void waitqueue_dump(wait_queue_t *wq);
#else
#define waitqueue_dump(wq)
#endif /* DEBUG_WAITQUEUE */

#endif /* __WAITQUEUE_H__ */
