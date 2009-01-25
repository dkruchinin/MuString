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
 * (c) Copyright 2009 Dan Kruchinin <dan.kruchinin@gmail.com>
 *
 * include/eza/amd64/tlb.h - AMD64-specific TLB management functions
 *
 */

#ifndef __ARCH_TLB_H__
#define __ARCH_TLB_H__

#include <mlibc/types.h>
#include <eza/task.h>
#include <eza/arch/ptable.h>
#include <eza/arch/asm.h>

static inline void __tlb_flush(void)
{
  write_cr3(read_cr3());
}

static inline void __tlb_flush_entry(uintptr_t vaddr)
{
  __asm__ volatile("invlpg (%0)"
                   :: "r" (vaddr));
}

static inline void tlb_flush(rpd_t *rpd)
{
  if (task_get_rpd(current_task()) == rpd)
    __tlb_flush();
}

static inline void tlb_flush_entry(rpd_t *rpd, uintptr_t vaddr)
{
  if (task_get_rpd(current_task()) == rpd)
    __tlb_flush_entry(vaddr);
}

#endif /* __ARCH_TLB_H__ */
