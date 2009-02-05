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
 * include/eza/waitqueue.h: Priority-sorted wait queue API and defenitions
 */

/* TODO DK: write appropriate description */
/**
 * @file include/eza/waitqueue.h
 * @author Dan Kruchinin
 * @brief Priority-sorted wait queue API and defenitions
 */

#ifndef __WAITQUEUE_H__
#define __WAITQUEUE_H__

#include <ds/list.h>
#include <ds/pqueue.h>
#include <eza/scheduler.h>
#include <eza/spinlock.h>
#include <eza/errno.h>
#include <mlibc/types.h>

struct __task_struct;

/**
 * @struct wqueue_t
 * @brief General structure of priority-sorted wait queue.
 * @see wqueue_task_t
 */
typedef struct __wait_queue {
  uint32_t num_waiters;
  pqueue_t pqueue;
} wqueue_t;

typedef struct __wqueue_task {
  wqueue_t *wq;
  struct __task_struct *task; /**< The task itself */
  pqueue_node_t pq_node;
  void *private;
} wqueue_task_t;

/**
 * @enum wqueue_insop_t
 * @brief Task insertion policies
 * @see waitqueue_insert
 */
typedef enum __wqueue_insop {
  WQ_INSERT_SIMPLE = 0, /**< Simple insertion into the wait queue. Task will be not pushed into sleep state */
  WQ_INSERT_SLEEP_INR,  /**< Task will be pushed into *interruptible* sleep state after insertion into the wait queue. */
  WQ_INSERT_SLEEP_UNR,  /**< Task will be pushed into *uninterruptible* sleep state after insertion */
} wqueue_insop_t;

/**
 * @enum wqueue_delop_t
 * @beief Policies of deletion tasks from the wait queue
 */
typedef enum __wqeue_delop {
  WQ_DELETE_SIMPLE = 0, /**< Delete task from the wait queue without changing task state */
  WQ_DELETE_WAKEUP,     /**< Wake up task after deletion. */
} wqueue_delop_t;

#define __wqueue_lock(wq)   spinlock_lock(&(wq)->pqueue.qlock)
#define __wqueue_unlock(wq) spinlock_unlock(&(wq)->pqueue.qlock)

static inline void waitqueue_initialize(wqueue_t *wq)
{
  wq->num_waiters = 0;
  pqueue_initialize(&wq->pqueue);
}

static inline bool waitqueue_is_empty(wqueue_t *wq)
{
  return !(wq->num_waiters);
}

static inline void waitqueue_prepare_task(wqueue_task_t *wq_task, struct __task_struct *task)
{
  wq_task->task = task;
  wq_task->wq = NULL;
}

#define waitqueue_push(wq, wq_task)                     \
  waitqueue_insert(wq, wq_task, WQ_INSERT_SLEEP_UNR)
#define waitqueue_push_intr(wq, wq_task)                \
  waitqueue_insert(wq, wq_task, WQ_INSERT_SLEEP_INR)

int waitqueue_pop(wqueue_t *wq, struct __task_struct **task);
int waitqueue_insert(wqueue_t *wq, wqueue_task_t *wq_task, wqueue_insop_t iop);
int waitqueue_delete(wqueue_task_t *wq_task, wqueue_delop_t dop);
wqueue_task_t *waitqueue_first_task(wqueue_t *wq);

int waitqueue_delete_core(wqueue_task_t *wq_task, wqueue_delop_t dop);
int waitqueue_insert_core(wqueue_t *wq, wqueue_task_t *wq_task, wqueue_insop_t iop);
wqueue_task_t *waitqueue_first_task_core(wqueue_t *wq);

#define CONFIG_DEBUG_WAITQUEUE /* FIXME DK: remove after debugging */
#ifdef CONFIG_DEBUG_WAITQUEUE
void waitqueue_dump(wqueue_t *wq);
#else
#define waitqueue_dump(wq)
#endif /* CONFIG_DEBUG_WAITQUEUE */


#define WQUEUE_DEFINE_PRIO(name)                \
    wqueue_t (name) = WQUEUE_INITIALIZE_PRIO(name)

#define WQUEUE_INITIALIZE_PRIO(name)                                  \
  {   .pqueue = PQUEUE_INITIALIZE((name).pqueue),                     \
      .num_waiters = 0,  }

#endif /* __WAITQUEUE_H__ */
