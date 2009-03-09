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
 * include/mlibc/assert.h: defines ASSERT macro similar to libc's assert(...)
 *
 */


#ifndef __ASSERT_H__
#define __ASSERT_H__

#include <config.h>
#include <eza/kernel.h>
#include <eza/arch/assert.h>

#define CT_ASSERT(cond) ((void)sizeof(char[1 - 2 * !(cond)]))
#define ASSERT(cond)                                    \
  do {                                                  \
    if (unlikely(!(cond))) {                            \
      ASSERT_LOW_LEVEL("[KERNEL ASSERTION] " #cond "\n" \
                       "    in %s:%s:%d\n", __FILE__,   \
                       __FUNCTION__, __LINE__);         \
  }                                                     \
} while (0)

#endif /* __ASSERT_H__ */

