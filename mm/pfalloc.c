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
 * mm/pfalloc.c: page frame allocation API
 *
 */

#include <mm/page.h>
#include <mm/page.h>
#include <mm/mmpool.h>
#include <mm/pfalloc.h>
#include <eza/arch/types.h>

page_frame_t *alloc_pages(int n, pfalloc_flags_t flags)
{
  page_frame_t *pages = NULL;
  mm_pool_t *pool = mmpools_get_pool(__pool_type(flags & PAGE_POOLS_MASK));

  pages = __pool_alloc_pages(pool, n);  
  return pages;
}

void free_pages(page_frame_t *pages)
{
  mm_pool_t *pool = mmpools_get_pool(pframe_pool_type(pages));
  __pool_free_pages(pool, pages);
}
