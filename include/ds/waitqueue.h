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
 * include/eza/waitqueue.h: Wait queue API and defenitions.
 */

#ifndef __WAITQUEUE_H__
#define __WAITQUEUE_H__

#include <ds/list.h>
#include <eza/task.h>
#include <eza/spinlock.h>
#include <eza/errno.h>
#include <eza/arch/types.h>


/**
 * @typedef int (*wqueue_cmp_func_t)(wait_queue_task_t *wqt1, wait_queue_task_t *wqt2)
 * @brief Wait queue custom compare function.
 *
 * If wait queue has WQ_CUSTOM type its comparison function must be defined explicitely
 * by the user. Compare function is called on each insertion into wait queue. Depending
 * on the code returning by comparision function, wait queue makes a descition where exactly
 * new task should be placed.
 * wqueue_cmp_func_t should return:
 *  1) negative integer if wqt1 is leaster than wqt2
 *  2) 0 if wqt1 is equal to wqt2
 *  3) positive integer if wqt1 is greater then wqt2
 *
 * @see wqueue_t
 * @see wqueue_task_t
 * @see wqueue_type_t
 */
typedef int (*wqueue_cmp_func_t)(wait_queue_task_t *wqt1, wait_queue_task_t *wqt2);

/**
 * @enum wqueue_type_t
 * @brief Wait queue type
 *
 * Wait queues differ from each other by their types.
 * Type of wait queue determines semantic of insertion new
 * tasks into the queue. Broadly speaking the *position* of the
 * the task in wait queue waiters list. 
 *
 * @see wqueue_t
 * @see wqueue_task_t
 * @see wqueue_cmp_func_t
 */
typedef enum __wqueue_type {
  WQ_PRIO,     /**< Priority semantic: more prioritized tasts placed before less prioritized */
  WQ_CUSTOM,   /**< User-defined insertion semantic */
} wqueue_type_t;

/**
 * @struct wqueue_t
 * @brief General wait queue structure
 * @see wqueue_type_t
 * @see wqueue_cmp_func_t
 * @see wqueue_task_t
 */
typedef struct __wait_queue {
  list_head_t waiters;        /**< A list of waiting tasks */
  wqueue_cmd_func_t cmp_func; /**< Custom wait queue tasks compare function */
  spinlock_t q_lock;          /**< Wait queue strucure lock */
  wqueue_type_t type;         /**< Wait queue type */
  uint32_t num_waiters;       /**< Total number of waiters in queue */
} wqueue_t;

/**
 * @struct wqueue_task_t
 * @brief task_t-wrapper structure that is insertend into the wait queue
 *
 * Before inserting new task into the wait queue it must be wrapped by
 * this structure using waitqueue_prepare_task()
 * Typically wqueue_task_t is created on the stack of task that is going to sleep.
 *
 * @see wqueue_t
 * @see wqueue_prepare_task
 * @see task_t
 */
typedef struct __wqueue_task {
  list_head_t head;  /**< A list of waiting tasts of the same priority */
  list_node_t node;  /**< A list node  */
  task_t *task;      /**< Task itself */
  wqueue_t *q;       /**< A pointer to parent wait queue */
  bool uspc_blocked; /**<  */
  uint8_t cflags;    /**< Customizable by user flags */ 
} wqueue_task_t;

/**
 * @enum wqueue_insop_t
 * @brief Task insertion policies
 * @see waitqueue_insert
 */
typedef enum __wqueue_insop {
  WQ_INSERT_SIMPLE = 0, /**< Simple insertion into the wait queue. Task will be not pushed into sleep state */
  WQ_INSERT_SLEEP,      /**< Task will be pushed into sleep state after insertion into the wait queue. */
} wqueue_insop_t;

/**
 * @enum wqueue_delop_t
 * @beief Policies of deletion tasks from the wait queue
 */
typedef enum __wqeue_delop {
  WQ_DELETE_SIMPLE = 0, /**< Delete task from the wait queue without changing task state */
  WQ_DELETE_WAKEUP,     /**< Wake up task after deletion. */
} wqueue_delop_t;

/**
 * @fn status_t waitqueue_push(wqueue_t *wq, wqueueu_task_t *wq_task)
 * @beief Insert @a wq_task into the @a wq wait queue and force task to sleep
 * @param wq      - A pointer to the wait queue task will be inserted to
 * @param wq_task - A pointer the task wrapped by wqueue_task_t that will be inserted
 *
 * @return 0 on success, negative error code on error.
 */

/**
 * @fn wqueue_pop(wqueue_t *wq)
 * @brief Pop *first* task from the wait queue @a wq and wake it up.
 * @param wq - A pointer to the wait queue task will be removed from.
 *
 * @return 0 on success, negative error code on error.
 */
#define waitqueue_pop(wq)                                           \
  waitqueue_delete(waitqueue_first_task(wq), WQ_DELETE_WAKEUP);

/**
 * @brief Check if wait queue is empty
 * @param wq - A pointer to the wait queue
 *
 * @return true if queue is empty and false otherwise.
 */
static inline bool waitqueue_is_empty(wqueue_t *wq)
{
  return !(wq->num_waiters);
}

/**
 * @brief Initialize a wait queue
 * @param wq      - A pointer to the wait queue structure
 * @param wq_type - A type of wait queue
 *
 * @return 0 on success, -EINVAL if @a wq_type is invalid.
 */
status_t waitqueue_initialize(wqueue_t *wq, wqueue_type_t *wq_type);

/**
 * @brief "Wrap" a task_t with wqueue_task_t strcutre and prepare it for insertion into wait queue
 * @param wq_task - A pointer to wqueue_task_t which will "wrap" task_t
 * @param task    - task_t that will be "wrapped".
 */
void waitqueue_prepare_task(wqueue_task_t *wq_task, task_t *task);

/**
 * @brief Insert new task into the wait queue
 * @param wq      - A wait queue task will be inserted to
 * @param wq_task - Inserted task(actually a pointer to wqueue_task_t that wraps needful task_t)
 * @param iop     - Type of insertion operation
 *
 * @return 0 on success, negative error code on failure
 * @see wqueue_insop_t
 */
status_t waitqueue_insert(wqueue_t *wq, wqueue_task_t *wq_task, wqueue_insop_t iop);

/**
 * @brief Dlete task from the wait queue
 * @param wq_task - A task that will be removed from the parent queue
 * @param dop     - Type of deletion operation
 *
 * @return 0 on success, negative error code value on error
 * @see wqueue_delop_t
 */
status_t waitqueue_delete(wqueue_task_t *wq_task, wqueue_delop_t dop);

/**
 * @brief Get the very first task located in the wait queue
 * @param wq - A pointer to the wait queue task will taken from
 */
wqueue_task_t *waitqueue_first_task(wqueue_t *wq);

#define DEBUG_WAITQUEUE /* FIXME DK: remove after debug */
#ifdef DEBUG_WAITQUEUE
void waitqueue_dump(wait_queue_t *wq);
#else
#define waitqueue_dump(wq)
#endif /* DEBUG_WAITQUEUE */

#endif /* __WAITQUEUE_H__ */
