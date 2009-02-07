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
 *
 * include/ds/iterator.h: Abstract iterator API
 *
 */

/**
 * @file include/ds/iterator.h
 * @brief Abstract iterator API
 *
 * A set of functions and macros that let you
 * to define an iterator. Result data structure(iterator)
 * will contain described by DEFINE_ITERATOR macro
 * "public data" (a set of fields that can be accessed
 * explicitly and are updated after each iteration) and
 * "private data" (is also called "iterator context". It is a set of
 * iterator type-dependent data which is needed for internal operations
 * of particular iterator type and doesn't have any sense for iterator user)
 *
 * @author Dan Kruchinin
 */

#ifndef __ITERATOR_H__
#define __ITERATOR_H__

#include <config.h>
#include <mlibc/string.h>
#include <mlibc/assert.h>
#include <eza/kernel.h>

/**
 * @enum iter_state
 * Possible iterator states.
 */
enum iter_state {
  ITER_RUN = 1, /**< Iterator is in "running" state. This means iteration can be continued */
  ITER_STOP     /**< Iterator was stopped. This means either iteration wasn't started or was finished */
};

/**
 * @def DEFINE_ITERATOR(name, members...)
 * @brief Define new iterator.
 *
 * This macro lets to define new iterator. Each iterator
 * has four common operations: first, last, next and prev.
 * The purpose of these operation is obvious. Each operation
 * is a function that must be defined explicitely. All iterator
 * functions has the following format:
 * @code
 *   void (*function)(name_iterator_t *iter);
 * @endcode
 *
 * DEFINE_ITERATOR will create an iterator type <name>_iterator_t.
 * <name>_iterator_t data strucure will contain a set of @a members
 * that were passed to DEFINE_ITERATOR after @a name argument. After
 * iterator data structure is created, each item from @a members set
 * can be assessed as a simple field in <name>_iterator_t structure.
 * For example:
 * @code
 *   DEFINE_ITERATOR(page_frame, page_frame_t *page_frame);
 * @endcode
 * defines page_frame_iterator_t which contains page_frame member.
 *
 * @param name       - A name of iterator.
 * @param members... - A set of public members 
 */
#define DEFINE_ITERATOR(name, members...)                   \
    typedef struct __iterator_##name {                      \
        members;                                            \
        void (*first)(struct __iterator_##name *iter);      \
        void (*last)(struct __iterator_##name *iter);       \
        void (*next)(struct __iterator_##name *iter);       \
        void (*prev)(struct __iterator_##name *iter);       \
        uint8_t state;                                      \
        unsigned int type;                                  \
        void *__ctx;                                        \
    } name##_iterator_t

/**
 * @def DEFINE_ITERATOR_TYPES(name, types...)
 * @brief Define types of particular iterator.
 *
 * Each iterator may have several subtypes(contexts).
 * DEFINE_ITERATOR_TYPES define a set of all posible
 * types(contexts) of defined earlier iterator @a name.
 * For example:
 * @code
 *   DEFINE_ITERATOR_TYPES(page_frame, PF_ITER_LIST,
 *                                     PF_ITER_ARRAY,
 *                                     PF_ITER_TREE);
 * @endcode
 * will define three context-dependent iterator types
 *
 * @param name     - A name of existent iterator
 * @param types... - Variable-length list of possible types.
 *
 * @see DEFINE_ITERATOR
 */
#define DEFINE_ITERATOR_TYPES(name, types...)   \
  enum __iterator_##name##_type {               \
    types,                                      \
  }                                             \

/**
 * @def DEFINE_ITERATOR_CTX(iter_name, ctx_type, members...)
 * @brief Define a context-dependent data of particular iterator.
 *
 * Iterator context is a set of iterator type-dependent data.
 * For example PF_ITER_LIST may have the following data:
 * @code
 *   DEFINE_ITERATOR_CTX(page_frame, PF_ITER_LIST,
 *                       list_node_t *first;
 *                       list_node_t *last;
 *                       list_node_t *cur);
 * @endcode
 *
 * @param iter_name  - A name of existent iterator
 * @param ctx_type   - Existent type of iterator with name @a name
 * @param members... - Variable-length set of cotext-dependent iterator members
 *
 * @see DEFINE_ITERATOR
 * @see DEFINE_ITERATOR_TYPES
 */
#define DEFINE_ITERATOR_CTX(iter_name, ctx_type, members...)    \
    ITERATOR_CTX(iter_name, ctx_type) {                         \
        members;                                                \
    }

/**
 * @def ITERATOR_CTX(iter_name, ctx_type)
 * @brief Returns a context type of particular iterator
 *
 * For example to get a type of context of particular iterator,
 * you may do the following:
 * @code
 *   ITERATOR_CTX(page_frame, PF_ITER_ARRAY) *iter_array_ctx;
 * @endcode
 * declares a pointer of PF_ITER_ARRAY type.
 *
 * @param iter_name - A name of existent iterator
 * @param ctx_type -  Existent context type of iterator with name @a name
 *
 * @see DEFINE_ITERATOR_TYPES
 * @see DEFINE_ITERATOR_CTX
 */
#define ITERATOR_CTX(iter_name, ctx_type)       \
  struct __##iter_name##_##ctx_type

/**
 * @def iter_first(iter)
 * Go to the first iterator member
 *
 * @param iter - A pointer to iterator
 */
#define iter_first(iter)                        \
  ((iter)->first(iter))

/**
 * @def iter_last(iter)
 * Go to the last iterator member
 *
 * @param iter - A pointer to iterator
 */
#define iter_last(iter)                         \
  ((iter)->last(iter))

/**
 * @def iter_next(iter)
 * Go to next iterator member
 *
 * @param iter - A pointer to iterator
 */
#define iter_next(iter)                         \
  ((iter)->next(iter))

/**
 * @def iter_prev(iter)
 * Go to previous iterator member
 *
 * @param iter - A pointer to iterator
 */
#define iter_prev(iter)                         \
  ((iter)->prev(iter))

/**
 * @def iter_isrunning(iter)
 * Determines if iterator is in "running state"
 *
 * @param iter - A pointer to iterator
 * @return Boolean
 *
 * @see iter_isstopped
 * @see iter_state
 */
#define iter_isrunning(iter)                    \
  ((iter)->state == ITER_RUN)

/**
 * @def iter_isstopped(iter)
 * Determines if iterator is stopped
 *
 * @param iter - A pointer to iterator
 * @return Boolean
 *
 * @see iter_isrunning
 * @see iter_state
 */
#define iter_isstopped(iter) ((iter)->state == ITER_STOP)

/**
 * @def iter_fetch_ctx(iter)
 * Fetch iterator type-dependent context.
 *
 * @param iter - A pointer to iterator
 * @return A pointer to iterator context
 */
#define iter_fetch_ctx(iter) ((iter)->__ctx)

/**
 * @def iter_set_ctx(iter, val)
 * Set iterator context
 *
 * @param iter - A pointer to iterator
 * @param val  - A pointer to iterator context
 */
#define iter_set_ctx(iter, val) ((iter)->__ctx = (val))

/**
 * @def iter_init(iter, __type)
 * Initialize iterator
 *
 * @param iter   - A pointer to iterator
 * @param __type - Existent type of iterator @a iter
 */
#define iter_init(iter, __type)                 \
  do {                                          \
    (iter)->type = (__type);                    \
    (iter)->state = ITER_STOP;                  \
    ASSERT((iter)->next != NULL);               \
    ASSERT((iter)->prev != NULL);               \
    ASSERT((iter)->first != NULL);              \
    ASSERT((iter)->last != NULL);               \
  } while (0)

/**
 * @def iterate_forward(iter)
 * Forward iteration.
 * @note Iterator must support @e first and @e next methods
 *
 * @param iter - A pointer to iterator.
 */
#define iterate_forward(iter)                   \
  for (iter_first(iter); iter_isrunning(iter); iter_next(iter))

/**
 * @def iterate_backward(iter)
 * Backward iteration.
 * @note Iterator must support @e last and @e prev methods
 *
 * @param iter - A pointer to iterator.
 */

#define iterate_backward(iter)                  \
  for (iter_last(iter); iter_isrunning(iter); iter_prev(iter))

#endif /* __ITERATOR_H__ */
