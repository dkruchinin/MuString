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
 * include/mm/mmap.h - Architecture independend memory mapping interface
 *
 */

#ifndef __MMAP_H__
#define __MMAP_H__

#include <mlibc/string.h>
#include <mm/page.h>
#include <mlibc/types.h>
#include <eza/kernel.h>
#include <eza/mutex.h>
#include <eza/spinlock.h>
#include <eza/arch/ptable.h>
#include <eza/arch/atomic.h>

/**
 * @typedef unsigned int mmap_flags_t
 * Memory mapping flags.
 */
typedef unsigned int mmap_flags_t;

enum {
  PROT_READ    = 0x01,
  PROT_WRITE   = 0x02,
  PROT_NONE    = 0x04,
  PROT_EXEC    = 0x08,
  PROT_NOCACHE = 0x10,
};

enum {
  MAP_FIXED   = 0x01,
  MAP_ANON    = 0x02,
  MAP_PRIVATE = 0x04,
  MAP_SHARED  = 0x08,
  MAP_PHYS    = 0x10,
};

status_t mmap_kern(uintptr_t va, page_idx_t first_page, int npages,
                   uint_t proto, uint_t flags);

#endif /* __MMAP_H__ */
