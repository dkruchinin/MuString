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
 * ds/pqueue.c - Simple waitqueue based on lists
 *
 * Simple small list-based priority queue. The queue is quite effictive
 * when maximum number of priorities is not too big. Since pqueue_node_t
 * is quite small it may be effectivelly allocated on stack.
 */


#include <config.h>
#include <ds/list.h>
#include <ds/pqueue.h>
#include <eza/spinlock.h>
#include <mlibc/kprintf.h>
#include <mlibc/types.h>

void pqueue_insert_core(pqueue_t *pq, pqueue_node_t *pq_node, prio_t prio)
{
  list_node_t *next = NULL;
  
  pq_node->prio = prio;
  list_init_head(&pq_node->head);
  next = list_head(&pq->queue);
  if (likely(!list_is_empty(&pq->queue))) {
    pqueue_node_t *n;

    list_for_each_entry(&pq->queue, n, node) {
      if (n->prio > pq_node->prio) {
        next = &n->node;
        break;
      }
      else if (n->prio == pq_node->prio) {
        next = list_head(&n->head);
        break;
      }
    }
  }

  list_add_before(next, &pq_node->node);
}

void pqueue_delete_core(pqueue_t *pq, pqueue_node_t *pq_node)
{
  pqueue_node_t *n;

  if (!list_is_empty(&pq_node->head)) {
    n = list_entry(list_node_first(&pq_node->head), pqueue_node_t, node);
    list_del(&n->node);
    list_add_before(&pq_node->node, &n->node);
    if (!list_is_empty(&pq_node->head))
      list_move(list_head(&n->head), list_head(&n->head), &pq_node->head);
  }

  list_del(&pq_node->node);
  list_init_head(&pq_node->head);
}

void pqueue_insert(pqueue_t *pq, pqueue_node_t *pq_node, prio_t prio)
{
  spinlock_lock(&pq->qlock);
  pqueue_insert_core(pq, pq_node, prio);
  spinlock_unlock(&pq->qlock);
}

void pqueue_delete(pqueue_t *pq, pqueue_node_t *pq_node)
{
  if (pqueue_is_empty(pq)) {
    kprintf_dbg("Attemption to remove a node %p from an empty priority queue %p\n", pq_node, pq);
    return;
  }

  spinlock_lock(&pq->qlock);
  pqueue_delete_core(pq, pq_node);
  spinlock_unlock(&pq->qlock);
}

pqueue_node_t *pqueue_pick_min(pqueue_t *pq)
{
  pqueue_node_t *min = NULL;

  if (!pqueue_is_empty(pq)) {
    spinlock_lock(&pq->qlock);
    min = pqueue_pick_min_core(pq);
    spinlock_unlock(&pq->qlock);
  }

  return min;
}

pqueue_node_t *pqueue_delete_min(pqueue_t *pq)
{
  pqueue_node_t *pqn = NULL;
  
  if (pqueue_is_empty(pq))
    return pqn;

  spinlock_lock(&pq->qlock);
  pqn = pqueue_delete_min_core(pq);
  spinlock_unlock(&pq->qlock);

  return pqn;
}
