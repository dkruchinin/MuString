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
 * include/ds/pqueue.h - Priority queue API definitions.
 */

#ifndef __PQUEUE_H__
#define __PQUEUE_H__

#include <config.h>
#include <ds/list.h>
#include <sync/spinlock.h>
#include <mstring/types.h>

typedef long prio_t;

typedef struct __pqueue_node {
  list_head_t head;
  list_node_t node;
  prio_t prio;
} pqueue_node_t;

typedef struct __pqueue {
  spinlock_t qlock;
  list_head_t queue;
} pqueue_t;

#define PQUEUE_DEFINE(pq_name)                      \
  pqueue_t (pq_name) = PQUEUE_INITIALIZE(pq_name)

#define PQUEUE_INITIALIZE(pq_name)                               \
  {   .qlock = SPINLOCK_INITIALIZE(__SPINLOCK_UNLOCKED_V),    \
      .queue = LIST_INITIALIZE((pq_name).queue), }

static inline void pqueue_initialize(pqueue_t *pq)
{
  spinlock_initialize(&pq->qlock);
  list_init_head(&pq->queue);
}

static inline bool pqueue_is_empty(pqueue_t *pq)
{
  return list_is_empty(&pq->queue);
}

static inline pqueue_node_t *pqueue_pick_min_core(pqueue_t *pq)
{
  pqueue_node_t *min = NULL;
  
  if (likely(!pqueue_is_empty(pq)))
    min = list_entry(list_node_first(&pq->queue), pqueue_node_t, node);

  return min;
}

void pqueue_insert(pqueue_t *pq, pqueue_node_t *pq_node, prio_t prio);
void pqueue_delete(pqueue_t *pq, pqueue_node_t *pq_node);
pqueue_node_t *pqueue_pick_min(pqueue_t *pq);
pqueue_node_t *pqueue_delete_min(pqueue_t *pq);
void pqueue_delete_core(pqueue_t *pq, pqueue_node_t *pq_node);
void pqueue_insert_core(pqueue_t *pq, pqueue_node_t *pq_node, prio_t prio);

static inline pqueue_node_t *pqueue_delete_min_core(pqueue_t *pq)
{
  pqueue_node_t *min = pqueue_pick_min_core(pq);
  
  if (min)
    pqueue_delete_core(pq, min);

  return min;
}

#endif /* __PQUEUE_H__ */
