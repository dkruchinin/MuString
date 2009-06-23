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

#include <config.h>
#include <mm/page.h>
#include <mm/mem.h>
#include <arch/pt_defs.h>
#include <arch/tlb.h>
#include <mstring/errno.h>
#include <mstring/types.h>

ptable_flags_t __ptbl_allowed_flags_mask;

static int populate_pagedir(pde_t *pde, ptable_flags_t flags)
{
  void *subdir;

  ASSERT(!pde_is_present(pde));
  subdir = pt_ops.alloc_pagedir();
  if (!subdir) {
    return ERR(-ENOMEM);
  }
  if (subdir ==(void *)0xffffffff836dc000UL) {
    kprintf("GOT IT TO %p\n", *(uintptr_t *)subdir);
  }

  pde_save(pde, virt_to_pframe_id(subdir), flags);
  if (subdir ==(void *)0xffffffff836dc000UL) {
    kprintf("GOT IT TO %p\n", *(uintptr_t *)subdir);
  }
  return 0;
}

static void depopulate_pagedir(pde_t *pde)
{
  ASSERT(pde_is_present(pde));
  pde_set_not_present(pde);
  pt_ops.free_pagedir(pde_get_current_dir(pde));
}

page_idx_t ptable_vaddr_to_pidx(rpd_t *rpd, uintptr_t vaddr,
                                /* OUT */ pde_t **retpde)
{
  uintptr_t va = PAGE_ALIGN_DOWN(vaddr);
  void *cur_dir = ROOT_PDIR_PAGE(rpd);
  pde_t *pde;
  int level;

  if (retpde) {
    *retpde = NULL;
  }
  for (level = PTABLE_LEVEL_LAST; level > PTABLE_LEVEL_FIRST; level--) {
    pde = pde_fetch(cur_dir, pde_offset2idx(va, level));
    if (!pde_is_present(pde))
      return PAGE_IDX_INVAL;

    cur_dir = pde_fetch_subdir(pde);
  }

  pde = pde_fetch(cur_dir, pde_offset2idx(va, PTABLE_LEVEL_FIRST));
  if (retpde) {
    *retpde = pde;
  }
  if (!pde_is_present(pde)) {
    return PAGE_IDX_INVAL;
  }

 return pde_fetch_page_idx(pde);
}

int ptable_map_page(rpd_t *rpd, uintptr_t addr,
                    page_idx_t pidx, ptable_flags_t flags)
{
  void *cur_dir = ROOT_PDIR_PAGE(rpd);
  pde_t *pde, *parent_pde = NULL;
  int level, ret = 0;
  bool pde_was_present;

  ASSERT_DBG(!(addr & PAGE_MASK));
  flags &= __ptbl_allowed_flags_mask;
  for (level = PTABLE_LEVEL_LAST; level > PTABLE_LEVEL_FIRST; level--) {
    pde = pde_fetch(cur_dir, pde_offset2idx(addr, level));
    if (!pde_is_present(pde)) {
      ret = populate_pagedir(pde, PTABLE_DEF_PDIR_FLAGS);
      if (ret) {
        return ret;
      }

      pagedir_ref(pde);
    }
    else if (pde ==(void *)0xffffffff836dc000UL) {
      kprintf("WTF? %p\n", *(uintptr_t *)pde);
    }

    if ((uintptr_t)pde == (uintptr_t)KERNEL_OFFSET) {
      kprintf("Parent pde = %p, %d, %p\n", parent_pde, pde_fetch_page_idx(parent_pde), ROOT_PDIR_PAGE(rpd));
    }
    parent_pde = pde;    
    cur_dir = pde_fetch_subdir(pde);
  }
  {
    page_frame_t *p = virt_to_pframe((void *)0xffffffff836dc000UL);
    if (pframe_number(p) == pidx && KERNEL_ROOT_PDIR() != rpd) {
      kprintf("MMAP to %d: %p, %d\n", rpd->vmm->owner->pid, addr, atomic_get(&p->refcount));
    }
  }
  pde = pde_fetch(cur_dir, pde_offset2idx(addr, PTABLE_LEVEL_FIRST));
  pde_was_present = pde_is_present(pde);
  pde_save(pde, pidx, flags);

  if (!pde_was_present) {
    pagedir_ref(parent_pde);
  }

  tlb_flush_entry(rpd, addr);
  return 0;
}

void ptable_unmap_page(rpd_t *rpd, uintptr_t addr)
{
  void *cur_dir  = ROOT_PDIR_PAGE(rpd);
  int level;
  bool need_depopulate = false;
  pde_t *pde, *dirspath[PTABLE_LEVEL_LAST];

  for (level = PTABLE_LEVEL_LAST; level > PTABLE_LEVEL_FIRST; level--) {
    pde = pde_fetch(cur_dir, pde_offset2idx(addr, level));
    if (!pde_is_present(pde))
      return;

    dirspath[level - 1] = pde;
    cur_dir = pde_fetch_subdir(pde);
  }

  pde = pde_fetch(cur_dir, pde_offset2idx(addr, PTABLE_LEVEL_FIRST));
  if (!pde_is_present(pde)) {
    return;
  }

  {
    page_frame_t *px = pframe_by_id(pde_fetch_page_idx(pde));
    page_frame_t *p = virt_to_pframe((void *)0xffffffff836dc000UL);
    if (p == px && KERNEL_ROOT_PDIR() != rpd) {
      kprintf("UNMAP to %d: %p, %d\n", rpd->vmm->owner->pid, addr, atomic_get(&p->refcount));
    }
  }

  pde_set_not_present(pde);
  for (level = PTABLE_LEVEL_FIRST; level < PTABLE_LEVEL_LAST; level++) {
    pde = dirspath[level];    
    if (need_depopulate) {
      cur_dir = pde_fetch_subdir(pde);
      kprintf("DEPOPULATE: %p\n", cur_dir);
      depopulate_pagedir(cur_dir);
      need_depopulate = false;
    }
    
    pagedir_unref(pde);
    if (!pagedir_get_ref(pde)) {
      need_depopulate = true;
    }
    else {
      break;
    }
  }

  tlb_flush_entry(rpd, addr);
}
