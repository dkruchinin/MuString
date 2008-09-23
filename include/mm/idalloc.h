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
 * include/mm/idalloc.h: Init-data memory allocator
 *
 */

#ifndef __IDALLOC_H__
#define __IDALLOC_H__

#include <config.h>
#include <mm/mmpool.h>
#include <mm/page.h>
#include <eza/spinlock.h>
#include <eza/arch/types.h>

typedef struct __idalloc_meminfo {
  char *mem;
  list_head_t avail_pages;
  list_head_t used_pages;
  spinlock_t lock;
  int npages;    
  bool is_enabled;
} idalloc_meminfo_t;

extern idalloc_meminfo_t idalloc_meminfo;

void idalloc_enable(mm_pool_t *pool);
void idalloc_disable(void);
void *idalloc(size_t size);

static inline bool idalloc_is_enabled(void)
{
  return idalloc_meminfo.is_enabled;
}

#endif /* __IDALLOC_H__ */
