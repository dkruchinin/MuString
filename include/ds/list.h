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
 * (c) Copyright 2006,2007,2008 MString Core Team <http://mstring.jarios.org>
 * (c) Copyright 2008 Dan Kruchinin <dan.kruchinin@gmail.com>
 */

/**
 * @file include/ds/list.h
 * Doubly-linked list based on list.h taken from linux-kernel
 *
 * @author Dan Kruchinin
 */

#ifndef __LIST_H__
#define __LIST_H__ 

#include <mstring/stddef.h>
#include <mstring/assert.h>
#include <mstring/milestones.h>

/**
 * @typedef struct __list_node list_node_t
 * @struct __list_node
 * @brief List node
 */
typedef struct __list_node {
  struct __list_node *next;
  struct __list_node *prev;
} list_node_t;

/**
 * @typedef struct __list_head list_head_t
 * @struct __list_head
 * @brief List head
 * Actually list_head_t is the same as list_node_t, but
 * they have different types though. That was done to prevent
 * potentional errors(notice that list head is a stub, it's never
 * tied with any real data and it's used only to determine where list
 * starts and where it ends)
 */
typedef struct __list_head {
  list_node_t head; /**< Head element of the list */
} list_head_t;

/**
 * @typedef struct __skiplist skip_list_t
 * @struct __skiplist
 * @brief Skiplist
 * Represents one node for the 'skiplist' abstraction (i.e. for the list
 * that can be both list head and list node).
 */
typedef struct __skiplist {
  list_head_t head;
  list_node_t node;
} skiplist_t;

/**
 * @def LIST_DEFINE(name)
 * @brief Define and initialize list head with name @a name
 * @param name - name of variable
 */
#define LIST_DEFINE(name)                                       \
  list_head_t (name) = LIST_INITIALIZE(name)

/**
 * @def LIST_INITIALIZE
 * @brief Initialize list head.
 * @param name - list name
 */
#define LIST_INITIALIZE(name)                   \
  { .head = { &(name).head, &(name).head } }

#define SKIPLIST_DEFINE(name)                   \
  skiplist_t (name) = SKIPLIST_INITIALIZE(name)

#define SKIPLIST_INITIALIZE(name)               \
  {                                             \
    .head = { &(name).head, &(name).head };     \
    .node = { MLST_LIST_NEXT, MLST_LIST_PREV }  \
  }

#define list_node2head(node)                    \
    ((list_head_t *)(node))

/**
 * @fn static inline void list_init_head(list_head_t *lst)
 * @brief Initialize list head
 * @param lst - a pointer to list head.
 */
static inline void list_init_head(list_head_t *lst)
{
  lst->head.next = lst->head.prev = &lst->head;
}

/**
 * @fn static inline void list_init_node(list_node_t *node)
 * @brief Initialize list node
 * @param node - a pointer to free(unattached from list) node.
 */
static inline void list_init_node(list_node_t *node)
{
  node->next = MLST_LIST_NEXT;
  node->prev = MLST_LIST_PREV;
}

static inline bool list_node_next_isbound(list_node_t *node)
{
  return (node->next != MLST_LIST_NEXT);
}

static inline bool list_node_prev_isbound(list_node_t *node)
{
  return (node->prev != MLST_LIST_PREV);
}

#define list_node_is_bound(node)                \
    (list_node_next_isbound(node) && list_node_prev_isbound(node))

/**
 * @def list_entry(lst, nptr)
 * @brief Get item that holds @a nptr node
 * @param list - A pointer to the list
 * @param nptr - A pointer to the node
 * @return A pointer to the object given node contains
 */
#define list_entry(node, type, member)                 \
  container_of(node, type, member)

/**
 * @def list_head(lst)
 * @brief Get head of the list
 * @param lst - a pointer to list_head_t
 * @return A pointer to header list_node_t
 */
#define list_head(lst)                          \
  (&(lst)->head)

/**
 * @def list_node_first(lst)
 * @brief Get list's first node
 * @param list - A pointer to the list_head_t
 * @return A pointer to the list first node
 */
#define list_node_first(lst)                    \
  ((lst)->head.next)

/**
 * @def list_node_last(lst)
 * @brief Get list's last node
 * @param list - A pointer to the list_head_t
 * @return A pointer to the list last node
 */
#define list_node_last(lst)                     \
  ((lst)->head.prev)

/**
 * @def list_add2head(lst, new)
 * @brief Add a node @a new to the head of the list
 * @param lst - A pointer to the list
 * @param new - A pointer to the list node
 */
#define list_add2head(lst, new)                 \
  list_add_before(list_node_first(lst), new)

/**
 * @def list_add2tail(lst, new)
 * @brief Add a node @a new to the tail of the list
 * @param lst - A pointer to the list
 * @param new - A pointer to node to add
 */
#define list_add2tail(lst, new)                         \
  list_add_before(list_head(lst), new)

/**
 * @def list_delfromhead(lst)
 * @brief Remove first element of the list
 * @param lst - A pointer to the list
 */
#define list_delfromhead(lst)					\
  list_del(list_node_first(lst))

/**
 * @def list_delfromtail(lst)
 * @brief Remove the last element of the list
 * @param list - A pointer to the list
 */
#define list_delfromtail(lst)					\
  list_del(list_node_last(lst))

/**
 * @def list_del(del)
 * @brief Remove node @a del from the list
 * @param del - A node to remove
 */
#define list_del(del)                           \
  (list_del_range(del, del))

/**
 * @def list_add_before(before, new)
 * @param brefore - The node before which @a new will be inserted
 * @param new     - A node to insert
 */
#define list_add_before(before, new)                  \
  (list_add_range(new, new, (before)->prev, before))

/**
 * @def list_add_after(after, new)
 * @param after - The node after which a new one will be inserted
 * @param new   - A node to insert
 */
#define list_add_after(after, new)              \
  (list_add_range(new, new, (after), (after)->next))

/**
 * @def list_move2head(to, from)
 * @brief Move all nodes from list @a from to the head of list @a to
 * @param to   - destination list
 * @param from - source list
 */
#define list_move2head(to, from)                \
  (list_move(list_head(to), list_node_first(to), from))

/**
 * @def list_move2tail(to, from)
 * @brief Move all nodes from list @a from to the tail of list @a to
 * @param to   - destination list
 * @param from - source list
 */
#define list_move2tail(to, from)                \
  (list_move(list_node_last(to), list_head(to), from))

/**
 * @def list_for_each(lst, liter)
 * @brief Iterate through each element of the list
 * @param lst   - A pointer to list head
 * @param liter - A pointer to list which will be used for iteration
 */
#define list_for_each(lst, liter)                                       \
  for (liter = list_node_first(lst);                                    \
       (liter) != list_head(lst); (liter) = (liter)->next)

/**
 * @def list_for_each_safe(lst, liter, save)
 * @brief Safe iteration through the list @a lst
 *
 * This iteration wouldn't be broken even if @a liter will be removed
 * from the list
 *
 * @param lst   - A pointer to the list head 
 * @param liter - A pointer to list node which will be used for iteration
 * @param save  - The same
 */
#define list_for_each_safe(lst, liter, save)                            \
  for (liter = list_node_first(lst), save = (liter)->next;              \
       (liter) != list_head(lst); (liter) = (save),                     \
         (save) = (liter)->next)

/**
 * @def list_for_each_entry(lst, iter, member)
 * @brief Iterate through each list node member
 * @param lst    - a pointer list head
 * @param iter   - a pointer to list entry using as iterator
 * @param member - name of list node member in the parent structure
 */
#define list_for_each_entry(lst, iter, member)                         \
  for (iter = list_entry(list_node_first(lst), typeof(*iter), member); \
       &iter->member != list_head(lst);                                \
       iter = list_entry(iter->member.next, typeof(*iter), member))
    

/**
 * @fn static inline int list_is_empty(list_t *list)
 * @brief Determines if list @a list is empty
 * @param list - A pointer to list to test
 * @return True if list is empty, false otherwise
 */
static inline int list_is_empty(list_head_t *list)
{
  return (list_node_first(list) == list_head(list));
}

/**
 * @fn static inline void list_add_range(list_node_t *first, list_node_t *last,
 *                                list_node_t *prev, list_node_t *next)
 * @brief Insert a range of nodes from @a frist to @a last after @a prev and before @a last
 * @param first - first node of range
 * @param last  - last node of range
 * @param prev  - after this node a range will be inserted
 * @param next  - before this node a range will be inserted 
 */
static inline void list_add_range(list_node_t *first, list_node_t *last,
                                  list_node_t *prev, list_node_t *next)
{
  next->prev = last;
  last->next = next;
  prev->next = first;
  first->prev = prev;
}

/* for internal usage */
static inline void __list_del_range(list_node_t *first, list_node_t *last)
{
  first->prev->next = last->next;
  last->next->prev = first->prev;
}

/**
 * @fn static inline list_del_range(list_node_t *first, list_node_t *last)
 * @brief Delete nodes from @a first to @a last from list.
 * @param fist - first node to delete
 * @param last - last node to delete
 */
static inline void list_del_range(list_node_t *first, list_node_t *last)
{
  __list_del_range(first, last);
  first->prev = MLST_LIST_PREV;
  last->next = MLST_LIST_NEXT;
}

/**
 * @fn static inline void list_cut_sublist(list_node_t *first, list_node_t *last)
 * @brief Cut a "sublist" started from @a first and ended with @a last
 *
 * A @e "sublist" is similar to ordinary list except it hasn't a head.
 * In other words it's a cyclic list in which all nodes are equitable.
 *
 * @param first - From this node sublist will be cutted
 * @param last  - The last node in the cutted sequence
 */
static inline void list_cut_sublist(list_node_t *first, list_node_t *last)
{
  __list_del_range(first, last);
  first->prev = last;
  last->next = first;
}

/**
 * @fn static inline void list_cut_head(list_head_t *head)
 * @brief Cut a head from the list and make a "sublist"
 * @param head - List's head that will be cutted.
 * @see list_cut_sublist
 * @see list_set_head
 */
static inline void list_cut_head(list_head_t *head)
{
  list_cut_sublist(list_node_first(head), list_node_last(head));
}

/**
 * @fn static inline void list_cut_head(list_head_t *head)
 * @brief Attach a head to the sublist @a cyclist
 * @param new_head - A head that will be attached
 * @param cyclist  - "sublist"
 * @see list_cut_sublist
 * @see list_set_head
 */
static inline void list_set_head(list_head_t *new_head, list_node_t *cyclist)
{
  list_add_range(cyclist, cyclist->prev,
                 list_node_first(new_head), list_node_last(new_head));
}

/**
 * @fn static inline void list_move(list_node_t *prev, list_node_t *next, list_head_t *from)
 * @brief Insert nodes of list @a from after @a prev and before @a next
 * @param prev - a node after which nodes of @a from will be inserted
 * @param next - a node before which nodes of @a from will be inserted
 */
static inline void list_move(list_node_t *prev, list_node_t *next, list_head_t *from)
{
  ASSERT(!list_is_empty(from));
  list_add_range(list_node_first(from), list_node_last(from),
                 prev, next);
  list_init_head(from);
}

/**
 * @fn static inline void list_replace(list_node_t *old,list_node_t *new)
 * @brief Replace node @a old with @a new (@a old is removed from the list)
 * @param old - a node to be replaced
 * @param new - a node to be inserted instead of the old one
 */
static inline void list_replace(list_node_t *old,list_node_t *new)
{
  old->prev->next=new;
  new->prev=old->prev;
  new->next=old->next;
  old->next->prev=new;

  old->prev = MLST_LIST_PREV;
  old->next = MLST_LIST_NEXT;
}

#endif /* __LIST_H__ */

