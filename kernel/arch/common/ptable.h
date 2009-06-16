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
 */

#ifndef __MSTRING_ARCH_PTABLE_H__
#define __MSTRING_ARCH_PTABLE_H__

#include <arch/pt_defs.h>
#include <mstring/types.h>

struct __rpd;
extern ptable_flags_t __ptbl_allowed_flags_mask;

page_idx_t ptable_vaddr_to_pidx(struct __rpd *rpd, uintptr_t vaddr,
                                /* OUT */ pde_t **retpde);
int ptable_map_page(struct __rpd *rpd, uintptr_t addr,
                    page_idx_t pidx, ptable_flags_t flags);
void ptable_unmap_page(struct __rpd *rpd, uintptr_t addr);

#endif /* __MSTRING_ARCH_PTABLE_H__ */
