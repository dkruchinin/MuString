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

#include <mm/page.h>
#include <eza/arch/types.h>

typedef struct __mmap_info {
  uintptr_t va_from;
  uintptr_t va_to;
  page_frame_iterator_t *pfi;
  uint_t mmap_prot;
  uint_t mmap_flags;
  uint_t ptable_flags;
} mmap_info_t;

#endif /* __VMM_H__ */
