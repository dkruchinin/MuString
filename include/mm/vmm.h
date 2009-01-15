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
 * include/mm/vmm.h - Architecture independend memory mapping API
 *
 */

#ifndef __VMM_H__
#define __VMM_H__

#include <ds/iterator.h>
#include <mm/page.h>
#include <mm/memobj.h>
#include <mlibc/types.h>
#include <eza/arch/mm_types.h>

typedef struct __mmap_info {
  uintptr_t va_from;
  uintptr_t va_to;
  page_frame_iterator_t *pfi;
  uint_t ptable_flags;
} mmap_info_t;

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
    uintptr_t space_start;
    uintptr_t space_end;
  } range_bounds;
  struct __vmm *parent_vmm;
  memobj_t *memobj;
  void *private;
  pid_t pf_handler;
  vmrange_flags_t flags;
} vmrange_t;

typedef struct __vmm {
  ttree_t vmranges;
  vmrange_t *lru_range;
  atomic_t vmm_users;
  rpd_t *prd;
  uintptr_t aspace_start;
  unsigned long num_vmrs;
} vmm_t;

rpd_t kernel_rpd;

#endif /* __VMM_H__ */
