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
 * include/eza/waitqueue.h: Wait queue API and defenitions
 */

#ifndef __WAITQUEUE_H__
#define __WAITQUEUE_H__

#include <ds/list.h>
#include <eza/task.h>
#include <eza/spinlock.h>
#include <eza/arch/types.h>

/**
 * @typedef int (*wqueu_cmp_func_t)(wqueue_task_t *wqt1, wqueue_task_t *wqt2)
 * @brief Wait queue custom comparison function.
 *
 * In case wait queue has type WQ_CUSTOM, its comparison function has to be
 * defined explicitly by the user. Comparison function is called on each
 * insertion into the wait queue. Inserted task position in the wait queue
 * depens on the value returned by comparison function. Function may return:
 * 1) negative integer - if @a wqt1 leaster than @a wqt2
 * 2) 0 - if @a wqt1 equals to @a wqt2
 * 3) positive integer - if @a wqt1 greater than @a wqt2
 *
 * @see wqueue_t
 * @see wqueue_task_t
 * @see wqueue_type_t
 */
typedef int (*wqueue_cmp_func_t)(wqueue_task_t *wqt1, wqueue_task_t *wqt2);

/**
 * @enum wqueue_type_t
 * @brief Wait queue type
 *
 * Wait queue type determines semantic of insertion tasks
 * into the queue.
 *
 * @see wqueue_t
 * @see wqueue_cmp_func_t
 */
typedef enum __wqueue_type {
  WQ_DEFAULT = 0, /**< Default insertion semantic: more prioritized tasks located closer to the queue head */
  WQ_CUSTOM,      /**< User-defined insertion semantic */
} wqueue_type_t;

/**
 * @struct wqueue_t
 * @brief Main wait queue(a queue of sleeping tasks) structure
 *
 * Default wait queue insertion policy is insertion by tasks priority.
 * More prioritized tasks are always closer to queue head than less prioritized.
 *
 * @see wqueue_type_t
 * @see wqueue_task_t
 * @see wqueue_cmp_func_t
 */
typedef struct __wqueue {
  list_head_t waiters;        /**< A list of sleeping tasks */
  wqueue_cmp_func_t cmp_func; /**< Tasks comparision function */
  wqueue_type_t type;         /**< Wait queue type */
  uint32_t num_waiters;       /**< Number of tasks in queue */
  spinlock_t q_lock;          /**< Spinlock protecting wait queue structure */
} wqueue_t;

/**
 * @struct wqueue_task_t
 * @brief An item of wait queue that wraps task_t structure
 *
 * Wait queue item may be inserted to or removed from the wait queue.
 * Before inserting task into the wait queue it must be wrapped by wqueue_task_t
 * strucure. Typically that kind of structures is allocated on the stack of task
 * that is going to insert itself into the queue.
 *
 * @see wqueue_t
 * @see task_t
 */
typedef struct __wqueue_task {
  list_head_t head; /**< A list of tasks of the same priority */
  list_node_t node; /**< A node of priority list */
  task_t *task;     /**< A pointer to task itself */
  wait_queue_t *q;  /**< A pointer to parent wait queue */
  uint8_t uflags;   /**< User-defined flags */
} wqueue_task_t;

/**
 * @enum wqueue_insop_t
 * @brief Insertion into the wait queue policies
 */
typedef enum __wqueue_insop {
  WQ_INSERT_SIMPLE = 0, /**< Simple insertion. Do not enforce task to sleep. */
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
