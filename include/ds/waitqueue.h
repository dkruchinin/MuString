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
#include <eza/scheduler.h>
#include <eza/task.h>
#include <eza/spinlock.h>
#include <eza/errno.h>
#include <eza/arch/types.h>

/**
 * @struct wqueue_t
 * @brief General structure of priority-sorted wait queue.
 * @see wqueue_task_t
 */
typedef struct __wait_queue {
  list_head_t waiters;  /**< A list of waiting tasks */
  spinlock_t q_lock;    /**< Wait queue strucure lock */
  uint32_t num_waiters; /**< Total number of waiters in queue */
  struct {
    deferred_sched_handler_t sleep_if_needful; /**< This function checks if task *really* must be forced to sleep */
    void (*pre_wakeup)(void *data);            /**< This function(if exists) makes some
                                                    actions that should be done *before* task will be waked up  */
  } acts;
} wqueue_t;

/**
 * @struct wqueue_task_t
 * @brief task_t-wrapper structure that is insertend into the wait queue
 *
 * Before inserting new task into the wait queue, it must be wrapped by
 * this structure using waitqueue_prepare_task()
 * Typically wqueue_task_t is created on the stack of task that is going to sleep.
 * @NOTE: wqueue_prepare_task *must* be called before each insertion of task into the wait queu
 *        It is not enough to prepare task only one time.
 *
 * @see wqueue_t
 * @see wqueue_prepare_task
 * @see task_t
 * @see TF_USPC_BLOCKED
 */
typedef struct __wqueue_task {
  list_head_t head;  /**< A list of waiting tasts of the same priority */
  list_node_t node;  /**< A list node  */
  task_t *task;      /**< The task itself */
  wqueue_t *q;       /**< A pointer to parent wait queue */
  bool uspc_blocked; /**< Saved state of TF_USPC_BLOCKED flag */
  uint8_t eflags;    /**< Event flags. May be customized by user */ 
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
 * @brief Check if wait queue is empty
 * @param wq - A pointer to the wait queue
 *
 * @return true if queue is empty and false otherwise.
 */
static inline bool waitqueue_is_empty(wqueue_t *wq)
{
  return !(wq->num_waiters);
}

/*
 * the following three functions are internals
 * they must never be called explicitly
 */
status_t __waitqueue_delete(wqueue_task_t *wq_task, wqueue_delop_t dop);
bool __wq_sleep_if_needful(void *data);
void __wq_pre_wakeup(void *data);

/**
 * @fn status_t waitqueue_push(wqueue_t *wq, wqueueu_task_t *wq_task)
 * @beief Insert @a wq_task into the @a wq wait queue and force task to sleep
 * @param wq      - A pointer to the wait queue task will be inserted to
 * @param wq_task - A pointer the task wrapped by wqueue_task_t that will be inserted
 *
 * @return 0 on success, negative error code on error.
 */
#define waitqueue_push(wq, wq_task)             \
    waitqueue_insert(wq, wq_task, WQ_INSERT_SLEEP)

/**
 * @brief Pop *first* task from the wait queue @a wq and wake it up.
 * @param wq   - A pointer to the wait queue task will be removed from.
 * @param task - An optional pointer to pointer to task_t where popped task will be written to.
 *
 * @return 0 on success, negative error code on error.
 */
status_t waitqueue_pop(wqueue_t *wq, task_t **task);
  
/**
 * @brief Initialize a wait queue
 *
 * After wait queue is initialized user may to override queue's
 * "sleep_if_needful" and "pre_wakeup" functions.
 * The first one is dereferred schedule function which checks
 * if task *really* have to be forced in sleeping state.
 * The other one is an optional function that may do any
 * custom actions *before* task is waked up.
 * @param wq      - A pointer to the wait queue structure
 */
void waitqueue_initialize(wqueue_t *wq);

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
static inline status_t waitqueue_delete(wqueue_task_t *wq_task, wqueue_delop_t dop)
{
  wqueue_t *wq = wq_task->q;
  status_t ret;

  kprintf("QUEUE:=> %p, task(%p)\n", wq, wq_task);
  spinlock_lock(&wq->q_lock);  
  ret = __waitqueue_delete(wq_task, dop);
  spinlock_unlock(&wq->q_lock);

  return ret;
}

/**
 * @brief Get the very first task located in the wait queue
 * @param wq - A pointer to the wait queue task will taken from
 */
wqueue_task_t *waitqueue_first_task(wqueue_t *wq);

int __wqueue_cmp_default(wqueue_task_t *wqt1, wqueue_task_t *wqt2);

#define CONFIG_DEBUG_WAITQUEUE /* FIXME DK: remove after debugging */
#ifdef CONFIG_DEBUG_WAITQUEUE
void waitqueue_dump(wqueue_t *wq);
#else
#define waitqueue_dump(wq)
#endif /* CONFIG_DEBUG_WAITQUEUE */


#define WQUEUE_DEFINE_PRIO(name)                \
    wqueue_t (name) = WQUEUE_INITIALIZE_PRIO(name)

#define WQUEUE_INITIALIZE_PRIO(name)                                  \
  {   .waiters = LIST_INITIALIZE((name).waiters),                     \
      .q_lock = SPINLOCK_INITIALIZE(__SPINLOCK_UNLOCKED_V),           \
      .num_waiters = 0,                                               \
      .acts = { __wq_sleep_if_needful, __wq_pre_wakeup },   }

#endif /* __WAITQUEUE_H__ */
