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
#include <eza/arch/types.h>

typedef struct __wait_queue {
  list_head_t waiters;
  uint32_t num_waiters;
  spinlock_t q_lock;
} wait_queue_t;

typedef uint8_t wqueue_flags_t;

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

#define waitqueue_push(wq, task)                \
  waitqueue_insert(wq, task, WQ_INSERT_SLEEP)

static inline status_t waitqueue_push(wait_queue_t *wq, wait_queue_task_t *wq_task)
{
  return waitqueue_insert(wq, wq_task, WQ_INSERT_SLEEP);
}

static inline status_t waitqueue_pop(wait_queue_t *wq)
{
  if (waitqueue_is_empty(wq))
    return -EINVAL;

  return waitqueue_delete(wq,
                          list_entry(list_node_first(&wq->head), wait_queue_task_t, node),
                          WQ_DELETE_WAKEUP);
}

static inline bool waitqueue_is_empty(wait_queue_t *wq)
{
  return !!(wq->num_waiters);
}

status_t waitqueue_initialize(wait_queue_t *wq);
status_t waitqueue_prepare_task(wait_queue_task_t *wq_task, task_t *task);
void waitqueue_insert(wait_queue_t *wq, wait_queue_task_t *wq_task, wqueue_insop_t iop);
void waitqueue_delete(wait_queue_t *wq, wait_queue_task_t *wq_task, wqueue_delop_t dop);

#endif /* __WAITQUEUE_H__ */
