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

#include <config.h>
#include <ds/ttree.h>
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

typedef struct __mem_object {
} mem_object_t;

#define VMR_PROTO_MASK 0x0F

typedef enum __vmrange_flags {
  VMR_READ     = 0x001,
  VMR_WRITE    = 0x002,
  VMR_EXEC     = 0x004,
  VMR_NOCACHE  = 0x008,
  VMR_FIXED    = 0x010,
  VMR_ANON     = 0x020,
  VMR_PRIVATE  = 0x040,
  VMR_SHARED   = 0x080,
  VMR_PHYS     = 0x100,
  VMR_STACK    = 0x200,
  VMR_POPULATE = 0x400,
  VMR_GENERIC  = 0x800,
} vmrange_flags_t;

struct __vmm;

typedef struct __vmragnge {
  struct {
    uintptr_t vmr_start;
    uintptr_t vmr_end;
  } range_bounds;
  struct __vmm *parent_vmm;
  vmrange_flags_t flags;
  pid_t pf_handler;  
} vmrange_t;

typedef struct __vmm {
  ttree_t mmap_tree;
  rpd_t *rpd;
  
} vmm_t;

status_t mmap_kern(uintptr_t va, page_idx_t first_page, int npages,
                   uint_t proto, uint_t flags);

#endif /* __MMAP_H__ */
