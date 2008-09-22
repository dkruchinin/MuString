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
 * include/ds/iterator.h: Abstract iterator API
 *
 *
 */


#ifndef __ITERATOR_H__
#define __ITERATOR_H__

#include <config.h>
#include <mlibc/string.h>
#include <mlibc/assert.h>
#include <eza/kernel.h>

enum iter_state {
  ITER_RUN = 1,
  ITER_STOP
};

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

#define DEFINE_ITERATOR_TYPES(name, types...)   \
  enum __iterator_##name##_type {               \
      types,                                    \
  }                                             \

#define DEFINE_ITERATOR_CTX(iter_name, ctx_type, members...)    \
    ITERATOR_CTX(iter_name, ctx_type) {                         \
        members;                                                \
    }

#define ITERATOR_CTX(iter_name, ctx_type)       \
  struct __##iter_name##_##ctx_type

#define iter_first(iter)     ((iter)->first(iter))
#define iter_last(iter)      ((iter)->last(iter))
#define iter_next(iter)      ((iter)->next(iter))
#define iter_prev(iter)      ((iter)->prev(iter))
#define iter_isrunning(iter) ((iter)->state == ITER_RUN)
#define iter_isstopped(iter) ((iter)->state == ITER_STOP)
#define iter_fetch_ctx(iter) ((iter)->__ctx)
#define iter_set_ctx(iter, val) ((iter)->__ctx = (val))

#define iter_init(iter, __type)                 \
  do {                                          \
    (iter)->type = (__type);                    \
    (iter)->state = ITER_STOP;                  \
    ASSERT((iter)->next != NULL);               \
    ASSERT((iter)->prev != NULL);               \
    ASSERT((iter)->first != NULL);              \
    ASSERT((iter)->last != NULL);               \
  } while (0)

#define iterate_forward(iter)                   \
  for (iter_first(iter); iter_isrunning(iter); iter_next(iter))

#define iterate_backward(iter)                  \
  for (iter_last(iter); iter_isrunning(iter); iter_prev(iter))

#endif /* __ITERATOR_H__ */
