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
 * So-called "early" memory allocator is used during very early
 * kernel boot stage when no memory allocators available in a system.
 * All data that need to be allocated dynamically during kernel
 * initialization uses it. So, it should have enough pages to satisfy
 * all kernel needs. Number of pages given to early allocator can be
 * configured via NUM_EALLOC_PAGES macro defined in ealloc.h.
 *
 */

#include <config.h>
#include <mm/page.h>
#include <mm/ealloc.h>
#include <mstring/kprintf.h>
#include <mstring/string.h>
#include <mstring/panic.h>
#include <mstring/assert.h>
#include <mstring/types.h>

INITDATA ealloc_data_t ealloc_data;

INITCODE void ealloc_init(uintptr_t space_start, ulong_t space_rest)
{
  uintptr_t tmp;

  if (unlikely(space_start & PAGE_MASK)) {
    panic("space_start pointer is not page aligned!");
  }

  memset(&ealloc_data, 0, sizeof(ealloc_data));
  ealloc_data.space_start = ealloc_data.space_cur = (char *)space_start;
  tmp = space_start + NUM_EALLOC_PAGES * PAGE_SIZE;
  if (unlikely(tmp > (space_start + space_rest))) {
    panic("Not enough space for ealloc: expected %ld byes, given %ld bytes.",
          NUM_EALLOC_PAGES * PAGE_SIZE, space_rest);
  }

  ealloc_data.pspace_rest = space_rest - NUM_EALLOC_PAGES * PAGE_SIZE;
  ealloc_data.pages = ealloc_data.pages_start =
    ealloc_data.space_start + NUM_EALLOC_PAGES * PAGE_SIZE;
  ealloc_data.flags = EALLOCF_APAGES | EALLOCF_ASPACE;
}

INITCODE void *ealloc_space(size_t bytes)
{
  char *p = ealloc_data.space_cur;

  if (unlikely(!(ealloc_data.flags & EALLOCF_ASPACE))) {
    return NULL;
  }
  if (unlikely((p + bytes) > ealloc_data.pages_start)) {
    return NULL;
  }

  ealloc_data.space_cur += bytes;
  return p;
}

INITCODE void *ealloc_page(void)
{
  char *p = ealloc_data.pages;

  if (unlikely(!(ealloc_data.flags & EALLOCF_APAGES))) {
    return NULL;
  }
  if (unlikely(ealloc_data.pspace_rest < PAGE_SIZE)) {
    return NULL;
  }

  ealloc_data.pspace_rest -= PAGE_SIZE;
  ealloc_data.pages += PAGE_SIZE;

  return p;
}

INITCODE void ealloc_dump(void)
{
  kprintf("=== [EALLOC DUMP] ===\n");
  kprintf("space_start = %p\nspace_end = %p\n",
          ealloc_data.space_start, ealloc_data.pages_start);
  kprintf("NUM_EALLOC_PAGES = %d\navail. space = %ld bytes\n",
          NUM_EALLOC_PAGES,
          ealloc_data.pages_start - ealloc_data.space_cur);
  kprintf("Allocated space: %ld bytes\n",
          ealloc_data.space_cur - ealloc_data.space_start);
  kprintf("pages = %p, pspace_rest = %ld\n",
          ealloc_data.pages, ealloc_data.pspace_rest);
  kprintf("Allocated pages: %d\n",
          (ealloc_data.pages - ealloc_data.pages_start) >> PAGE_WIDTH);
}

