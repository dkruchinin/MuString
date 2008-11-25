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
 * eza/amd64/mm/ptable.c - AMD64-specific page-table management API
 *
 */

#include <mlibc/assert.h>
#include <mm/page.h>
#include <mm/pfalloc.h>
#include <mm/mmap.h>
#include <eza/spinlock.h>
#include <eza/arch/ptable.h>
#include <eza/arch/types.h>

pde_flags_t pgt_translate_flags(unsigned int mmap_flags)
{
  pde_flags_t flags = 0;

  CT_ASSERT(sizeof(mmap_flags) >= sizeof(mmap_flags_t));
  if (mmap_flags & MAP_USER)
    flags |= PDE_US;
  if (mmap_flags & MAP_WRITE)
    flags |= PDE_RW;
  if (mmap_flags & MAP_DONTCACHE)
    flags |= PDE_PCD;
  if (!(mmap_flags & MAP_EXEC))
    flags |= PDE_NX;

  return flags;
}

page_frame_t *pgt_create_pagedir(page_frame_t *parent, pdir_level_t level)
{
  page_frame_t *dir = alloc_page(AF_ZERO | AF_PGEN);
  
  if (dir)
    mm_pagedir_initialize(dir, parent, level);

  return dir;
}

