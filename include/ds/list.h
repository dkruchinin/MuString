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
 */

/**
 * @file include/ds/list.h
 * Doubly-linked list based on list.h taken from linux-kernel
 */

#ifndef __LIST_H__
#define __LIST_H__ 

#include <mlibc/stddef.h>
#include <mlibc/assert.h>
#include <eza/milestones.h>

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
 * @def LIST_DEFINE(name)
 * @brief Define and initialize list head with name @a name
 * @param name - name of variable
 */
#define LIST_DEFINE(name)                                       \
  list_head_t name = { .head = { &(name).head, &(name).head } }

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
  list_add(list_node_first(lst), new)

/**
 * @def list_add2tail(lst, new)
 * @brief Add a node @a new to the tail of the list
 * @param lst - A pointer to the list
 * @param new - A pointer to node to add
 */
#define list_add2tail(lst, new)                         \
  list_add(list_head(lst), new)

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
 * @def list_add(before, new, after)
 * @param after  - will be the next node after @a new
 * @param new    - node to insert
 */
#define list_add(after, new)                        \
  (list_add_range(new, new, (after)->prev, after))

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

/**
 * @fn static inline list_del_range(list_node_t *first, list_node_t *last)
 * @brief Delete nodes from @a first to @a last from list.
 * @param fist - first node to delete
 * @param last - last node to delete
 */
static inline void list_del_range(list_node_t *first, list_node_t *last)
{
  first->prev->next = last->next;
  last->next->prev = first->prev;  
  first->prev = MLST_LIST_PREV;
  last->next = MLST_LIST_NEXT;
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

#endif /* __LIST_H__ */

