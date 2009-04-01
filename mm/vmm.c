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
 * mm/vmm.c: Basic VMM implementation.
 *
 */

#include <ds/iterator.h>
#include <ds/list.h>
#include <mm/page.h>
#include <mm/mmpool.h>
#include <mm/pfalloc.h>
#include <mm/idalloc.h>
#include <mm/vmm.h>
#include <mm/mem.h>
#include <mm/memobj.h>
#include <mm/rmap.h>
#include <mlibc/types.h>

#ifdef CONFIG_DEBUG_MM
static inline void __check_one_frame(mm_pool_t *p, page_frame_t *pf)
{
  if (pf->pool_type == p->type) {
    kprintf("[FAILED]\n");
    panic("Page frame #%#x doesn't included into memory pool "
          "\"%s\", but indeed that frame belongs to it!",
          pframe_number(pf), p->name);
  }
}

static void __validate_mmpools_dbg(void)
{
  mm_pool_t *p;
  page_idx_t total_pages = 0, pool_total, pool_reserved, i;
  page_frame_t *pf;

  kprintf("[DBG] Validating memory pools... ");
  for_each_mm_pool(p) {
    pool_total = pool_reserved = 0;
    /* check all pages belonging to given memory pool */
    for (i = 0; i < p->total_pages; i++, pool_total++) {
      pf = pframe_by_number(i + p->first_page_id);
      if (pf->flags & PF_RESERVED)
        pool_reserved++;
      if (pf->pool_type != p->type) {
        mm_pool_t *page_pool = get_mmpool_by_type(pf->pool_type);

        kprintf("[FAILED]\n");
        if (!page_pool) {
            panic("Page frame #%#x belongs to memory pool \"%s\", "
                  "but in a \"pool_type\" field it has unexistent pool "
                  "type: %d!", i + p->first_page_id, p->name, pf->pool_type);
        }

        panic("Memory pool \"%s\" says that page frame #%#x "
              "belongs to it, but its owner is memory pool \"%s\" "
              "indeed!", p->name, i + p->first_page_id, page_pool->name);
      }
    }
    if (pool_total != p->total_pages) {
      kprintf("[FAILED]\n");
      panic("Memory pool \"%s\" has inadequate total "
            "number of pages it owns(%d): %d was expected!",
            p->name, p->total_pages, pool_total);
    }
    if (pool_reserved != p->reserved_pages) {
      kprintf("[FAILED]\n");
      panic("Memory pool \"%s\" has inadequate number "
            "of reserved pages (%d): %d was expected!",
            p->name, p->reserved_pages, pool_reserved);
    }
    if ((atomic_get(&p->free_pages) != (pool_total - pool_reserved)) &&
        (p->type != BOOTMEM_POOL_TYPE)) {
      kprintf("[FAILED]\n");
      panic("Memory pool \"%s\" has inadequate number of "
            "free pages (%d): %d was expected!",
            p->name, atomic_get(&p->free_pages), pool_total - pool_reserved);
    }
    if (p->first_page_id != PAGE_IDX_INVAL) {
      if (p->first_page_id != 0)
        __check_one_frame(p, pframe_by_number(p->first_page_id - 1));
      if ((p->first_page_id + p->total_pages) < num_phys_pages)
          __check_one_frame(p, pframe_by_number(p->first_page_id
                                                + p->total_pages));
    }

    total_pages += pool_total;
  }
  if (total_pages != num_phys_pages) {
    kprintf("aga! 5\n");
    kprintf("[FAILED]\n");
    panic("Unexpected total number of pages owned "
          "by different memory pools: %d, but %d was expected!",
          total_pages, num_phys_pages);
  }

  kprintf("[OK]\n");
}
#else
#define __validate_mmpools_dbg()
#endif /* CONFIG_DEBUG_MM */

void mm_initialize(void)
{
  mm_pool_t *pool;
  int activated_pools = 0;
  pfalloc_flags_t old_flags;

  mmpools_initialize();
  arch_mm_init();

  mmpool_activate(POOL_BOOTMEM());
  old_flags = ptable_ops.alloc_flags;
  ptable_ops.alloc_flags = AF_BMEM | AF_ZERO;
  if (initialize_rpd(&kernel_rpd, NULL))
    panic("mm_init: Can't initialize kernel root page directory!");

  /* Now we can remap available memory */
  arch_mm_remap_pages();
  ptable_ops.alloc_flags = old_flags;
  for_each_mm_pool(pool) {
    if (!atomic_get(&pool->free_pages) || (pool->type == BOOTMEM_POOL_TYPE))
      continue;

    kprintf("activate pool %d %s\n", pool->type, pool->name);
    mmpool_activate(pool);
    activated_pools++;
  }
  if (!activated_pools)
    panic("No one memory pool was activated!");

  for_each_active_mm_pool(pool) {
    kprintf("[MM] Pages statistics of pool \"%s\":\n", pool->name);
    kprintf(" | %-8s %-8s %-8s |\n", "Total", "Free", "Reserved");
    kprintf(" | %-8d %-8d %-8d |\n", pool->total_pages,
            atomic_get(&pool->free_pages), pool->reserved_pages);
  }

  kprintf("[MM] All pages were successfully remapped.\n");
  __validate_mmpools_dbg();
}

void vmm_initialize(void)
{
  memobj_subsystem_initialize();
  vmm_subsystem_initialize();
  rmap_subsystem_initialize();
}

/*
 * This function must be called after page table lock is aquired.
 * It guaranties that src_page refcount will be at least equal to 1.
 * This prevents tasks from simultaneously copying and(later)
 * freeing the same page. dst_page *must* be already pinned.
 */
int handle_copy_on_write(vmrange_t *vmr, uintptr_t addr,
                         page_frame_t *dst_page, page_frame_t *src_page)
{
  int ret;
  vmm_t *vmm = vmr->parent_vmm;
  page_idx_t pidx;

  ASSERT_DBG(!(vmr->flags & (VMR_PHYS | VMR_NONE)));
  ASSERT_DBG((addr >= vmr->bounds.space_start) &&
             (addr < vmr->bounds.space_end));

  pidx = vaddr2page_idx(&vmm->rpd, addr);

  /*
   * Check if another thread hasn't already made all work itself.
   * If so, there is nothing to do here anymore.
   */
  if (unlikely(pidx != pframe_number(src_page))) {
    return 0;
  }

  copy_page_frame(dst_page, src_page);
  dst_page->offset = src_page->offset;

  lock_page_frame(src_page, PF_LOCK);
  ret = rmap_unregister_shared(src_page, vmm, addr);
  if (ret) {
    unlock_page_frame(src_page, PF_LOCK);
    return ret;
  }

  unlock_page_frame(src_page, PF_LOCK);
  unpin_page_frame(src_page);

  ret = mmap_one_page(&vmm->rpd, addr, pframe_number(dst_page), vmr->flags);
  if (ret)
    return ret;

  /*
   * Ok, page is mapped now and we're free to register new anonymous
   * reverse mapping for it.
   */
  ret = rmap_register_anon(dst_page, vmm, addr);
  return ret;
}

int prepare_page_for_cow(vmrange_t *vmr, page_frame_t *page, uintptr_t addr)
{
  int ret = 0;
  vmm_t *vmm = vmr->parent_vmm;
  memobj_t *memobj = vmr->memobj;

  ASSERT_DBG((addr >= vmr->bounds.space_start) &&
             (addr < vmr->bounds.space_end));
  ASSERT_DBG(!(page->flags | PF_COW));

  /* There is nothing to do with physical mappings. We event don't need */
  if (unlikely(vmr->flags & (VMR_PHYS | VMR_SHARED)))
    return 0;

  lock_page_frame(page, PF_LOCK);
  
  /*
   * In simplest case the page that will be marked with
   * PF_COW flag is changing its state and state of its
   * reverse mapping. I.e. it was a part of anonymous mapping, but
   * after PF_COW flag is set it becomes a part of
   * shared(between parent and child[s]) mapping.
   * If so, its refcount must be 1.
   */
  if (likely(atomic_get(&page->refcount) == 1)) {
    ASSERT(!(page->flags & PF_COW));
    /* clear old anonymous rmap */
    rmap_unregister_anon(page, vmm, addr);
    /* PF_COW flag will tell memory object about nature of the fault */
    page->flags |= PF_COW;
    /* and create a new one(shared) */
    ret = rmap_register_shared(memobj, page, vmm, addr);
    if (ret) {
      unlock_page_frame(page, PF_LOCK);
      return ret;
    }
  }
  else {
    /*
     * In other case page already has PF_COW flag and
     * thus its reverse mapping is already shared. Actually
     * it's a quite common situation: for example one process
     * has done fork and has born a new child that shares most
     * of its pages with parent. Then a child does fork to born
     * another one, that will share its COW'ed pages wit
     * its parent and grandparent.
     */
    ASSERT(page->flags & PF_COW);
    ret = rmap_register_shared(memobj, page, vmm, addr);
  }

  unlock_page_frame(page, PF_LOCK);
  return ret;
}

int mmap_kern(uintptr_t va_from, page_idx_t first_page, pgoff_t npages, long flags)
{
  pgoff_t i;
  int ret = 0;

  pagetable_lock(&kernel_rpd);
  for (i = 0; i < npages; i++, va_from += PAGE_SIZE, first_page++) {    
    ret = mmap_one_page(&kernel_rpd, va_from, first_page, flags);
    if (ret)
      goto out;
  }

out:
  pagetable_unlock(&kernel_rpd);
  return ret;
}
