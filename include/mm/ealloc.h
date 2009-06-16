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
 */

#ifndef __MSTRING_EALLOC_H__
#define __MSTRING_EALLOC_H__

#include <mm/page.h>
#include <mstring/types.h>

/*
 * Default number of pages given to ealloc for
 * allocation of memory chunks smaller than PAGE_SIZE
 */
#define NUM_EALLOC_PAGES 4

#define EALLOCF_APAGES 0x01
#define EALLOCF_ASPACE 0x02

typedef struct ealloc_data {
  char *space_start;
  char *space_cur;
  char *pages_start;
  char *pages;
  ulong_t pspace_rest;
  uint8_t flags;
} ealloc_data_t;

extern INITDATA ealloc_data_t ealloc_data;

INITCODE void ealloc_init(uintptr_t space_start, ulong_t space_rest);
INITCODE void *ealloc_space(size_t bytes);
INITCODE void *ealloc_page(void);
INITCODE void ealloc_dump(void);

static inline void ealloc_disable_feature(uint8_t feature)
{
  ealloc_data.flags &= ~feature;
}

#endif /* __MSTRING_EALLOC_H__ */
