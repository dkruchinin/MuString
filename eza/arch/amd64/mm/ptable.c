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
 * eza/arch/amd64/mm/ptable.c - AMD64-specific page table functionality.
 *
 */

#include <config.h>
#include <mlibc/assert.h>
#include <ds/iterator.h>
#include <ds/list.h>
#include <mm/page.h>
#include <mm/pfi.h>
#include <mm/pfalloc.h>
#include <mm/ptable.h>
#include <mm/vmm.h>
#include <eza/errno.h>
#include <mlibc/types.h>
#include <eza/genarch/ptable.h>
#include <eza/arch/ptable.h>
#include <eza/arch/tlb.h>
#include <eza/arch/mm.h>

static int initialize_rpd(rpd_t *rpd)
{
  rpd->pml4 = generic_create_pagedir();
  if (!rpd->pml4)
    return -ENOMEM;

  return 0;
}

static void deinitialize_rpd(rpd_t *rpd)
{
  atomic_set(&rpd->pml4->refcount, 0);
  free_page(rpd->pml4);
  rpd->pml4 = NULL;
}

static void clone_rpd(rpd_t *clone, rpd_t *src)
{
  clone->pml4 = src->pml4;
  pin_page_frame(clone->pml4);
}

ptable_ops_t ptable_ops = {
  .initialize_rpd = initialize_rpd,
  .deinitialize_rpd = deinitialize_rpd,
  .clone_rpd = clone_rpd,
  .mmap = generic_ptable_map,
  .munmap = generic_ptable_unmap,
  .vaddr2page_idx = generic_vaddr2page_idx,
  .alloc_flags = AF_ZERO,
};

ptable_flags_t kmap_to_ptable_flags(uint32_t kmap_flags)
{
  ptable_flags_t ptable_flags = 0;

  ptable_flags |= (!!(kmap_flags & KMAP_WRITE) << pow2(PDE_RW));
  ptable_flags |= (!(kmap_flags & KMAP_KERN) << pow2(PDE_US));
  ptable_flags |= (!!(kmap_flags & KMAP_NOCACHE) << pow2(PDE_PCD));
  ptable_flags |= (!(kmap_flags & KMAP_EXEC) << pow2(PDE_NX));

  return ptable_flags;
}

uint32_t ptable_to_kmap_flags(ptable_flags_t flags)
{
  uint32_t kmap_flags = KMAP_READ;

  kmap_flags |= (!!(flags & PDE_RW) << pow2(KMAP_WRITE));
  kmap_flags |= (!(flags & PDE_US) << pow2(KMAP_KERN));
  kmap_flags |= (!!(flags & PDE_PCD) << pow2(KMAP_NOCACHE));
  kmap_flags |= (!(flags & PDE_NX) << pow2(KMAP_EXEC));

  return kmap_flags;
}
