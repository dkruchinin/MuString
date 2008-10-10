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
 *
 * include/eza/waitqueue.h: prototypes for waitqueues.
 */

#ifndef __WAITQUEUE_H__
#define  __WAITQUEUE_H__

#include <eza/task.h>
#include <eza/spinlock.h>
#include <ds/list.h>
#include <eza/arch/types.h>

typedef struct __wait_queue {
  list_head_t waiters;
  ulong_t num_waiters;
  spinlock_t q_lock;
} wait_queue_t;

typedef struct __wait_queue_task {
  list_node_t l;
  task_t *task;
  ulong_t flags;  
} wait_queue_task_t;

#define WQ_EVENT_OCCURED  0x1

#define LOCK_WAITQUEUE(w)                       \
    interrupts_disable();                       \
    spinlock_lock(&w->q_lock)

#define UNLOCK_WAITQUEUE(w)                     \
    spinlock_unlock(&w->q_lock);                \
    interrupts_enable();                          

void waitqueue_initialize(wait_queue_t *wq);
void waitqueue_add_task(wait_queue_t *wq,wait_queue_task_t *w);
bool waitqueue_is_empty(wait_queue_t *wq);
ulong_t waitqueue_get_tasks_number(wait_queue_t *wq);
void waitqueue_wake_one_task(wait_queue_t *wq);
void waitqueue_yield(wait_queue_task_t *w);

#endif
