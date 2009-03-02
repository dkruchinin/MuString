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
 * mm/memobj_pagecacge.c - Pagecached memory object implementation
 *
 */

#include <config.h>
#include <ds/list.h>
#include <ds/hat.h>
#include <mm/vmm.h>
#include <mm/memobj.h>
#include <mm/pfalloc.h>
#include <mm/ptable.h>
#include <mlibc/stddef.h>
#include <mlibc/types.h>

static memobj_ops_t pcacge_memobj_ops {
  .handle_page_fault = pcache_handle_page_fault;
  .populate_pages = pcache_populate_pages;
  .put_page = pcache_put_page;
  .get_page = pcache_get_page;
};

int pcache_memobj_initialize(memobj_t *memobj, uint32_t flags)
{
  if (!(flags & MMO_LIVE_MASK) || !is_powerof_2(flags & MMO_LIVE_MASK))
}
