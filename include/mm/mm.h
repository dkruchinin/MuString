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
 * (c) Copyright 2008 Michael Tsymbalyuk <mtzaurus@gmail.com>
 * (c) Copyright 2008 Dan Kruchinin <dan.kruchinin@gmai.com>
 *
 * include/mm/mm.h: Contains types and prototypes for the kernel memory manager.
 *
 */

/**
 * @file include/mm/mm.h
 * Architecture-independent memory manager API.
 */

#ifndef __MM_H__
#define __MM_H__

#include <ds/iterator.h>
#include <mm/page.h>
#include <mm/pfalloc.h>
#include <eza/arch/mm.h>
#include <eza/arch/types.h>

extern page_frame_t *page_frames_array; /**< An array of all available physical pages */
extern uintptr_t kernel_min_vaddr; /**< The bottom address of kernel virtual memory space. */

static inline void pin_page_frame(page_frame_t *pf)
{
  atomic_inc(&pf->refcount);
}

static inline void unpin_page_frame(page_frame_t *pf)
{
  ASSERT(atomic_get(&pf->refcount) != 0);
  atomic_dec(&pf->refcount);
  if (!atomic_get(&pf->refcount))
    free_pages(pf);
}

static inline bool uspace_varange_is_valid(uintptr_t va_start, uintptr_t length)
{
  return ((va_start >= USER_START_VIRT) &&
          ((va_start + length) < USER_END_VIRT));
}

#define get_user(a,_uptr)  copy_from_user(&(a),_uptr,sizeof((a)))
#define put_user(a,_uptr)  copy_to_user(_uptr,&(a),sizeof((a)))

/**
 * @brief Initialize mm internals
 * @note It's an initcall, so it should be called only once during system boot stage.
 */
void mm_init(void);
status_t copy_to_user(void *dest,void *src,ulong_t size);
status_t copy_from_user(void *dest,void *src,ulong_t size);

#endif /* __MM_H__ */

