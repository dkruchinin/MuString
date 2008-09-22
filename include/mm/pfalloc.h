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
 * include/mm/pfallocc.h: Contains types and prototypes for kernel page
 *                         allocator.
 *
 */

#ifndef __PFALLOC_H__
#define __PFALLOC_H__ 

#include <mm/page.h>
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
 * include/mm/pfalloc.h: page frame allocation API
 *
 */

#include <mm/page.h>
#include <eza/arch/types.h>

#define AF_PDMA PF_PDMA
#define AF_PGP  PF_PGP

typedef uint8_t pfalloc_flags_t;

typedef enum __pfalloc_type {
  PFA_TLSF = 1,
} pfalloc_type_t;

typedef struct __pf_allocator {
  page_frame_t *(*alloc_pages)(int n, void *data);
  void (*free_pages)(page_frame_t *pframe, int n, void *data);  
  void *alloc_ctx;
  pfalloc_type_t type;
} pf_allocator_t;

#define alloc_page(flags)                       \
  alloc_pages(1, flags)
#define free_page(page)                         \
  free_pages(page, 1)

page_frame_t *alloc_pages(int n, pfalloc_flags_t flags);
void free_pages(page_frame_t *pages, int n);

#endif /* __PFALLOC_H__ */

