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
 * mm/mmap.c - Architecture independend memory mapping API
 *
 */

#include <config.h>
#include <ds/list.h>
#include <ds/ttree.h>
#include <mm/page.h>
#include <mm/page_alloc.h>
#include <mm/slab.h>
#include <mm/memobj.h>
#include <mm/vmm.h>
#include <mm/mman.h>
#include <mm/rmap.h>
#include <sync/rwsem.h>
#include <sync/spinlock.h>
#include <mstring/usercopy.h>
#include <mstring/kprintf.h>
#include <mstring/types.h>
#include <mstring/process.h>

#define INVALID_ADDRESS (~0UL)

/* kernel root page directory */
rpd_t kernel_rpd;

/* Memory cache for vmm_t structures */
static memcache_t *__vmms_cache = NULL;

/* Memory cache for vmrange_t structures */
static memcache_t *__vmrs_cache = NULL;

/* Mandatory mappings list */
static LIST_DEFINE(__mandmaps_lst);

/* Total number of mandatory mappings */
static int __num_mandmaps = 0;

#ifdef CONFIG_DEBUG_MM
static bool __vmm_verbose = false;
static SPINLOCK_DEFINE(__vmm_verb_lock, "VMM verbose");

#define VMM_VERBOSE(fmt, args...)               \
  do {                                          \
    if (__vmm_verbose) {                        \
      kprintf("[VMM VERBOSE]: ");               \
      kprintf(fmt, ##args);                     \
    }                                           \
  } while (0)

#else
#define VMM_VERBOSE(fmt, args...)
#endif /* CONFING_DEBUG_MM */

static inline bool __valid_vmr_rights(vmrange_t *vmr, uint32_t pfmask)
{
  return !(((pfmask & PFLT_WRITE) &&
            ((vmr->flags & (VMR_READ | VMR_WRITE)) == VMR_READ))
           || (vmr->flags & VMR_NONE) || (pfmask & PFLT_NOEXEC));
}

/*
 * Function for comparing VM ranges by their keys.
 * The key of VM range is a diapason [start_address, end_address).
 * Function returns negative integer if range r1 is leaster than r2,
 * positive if r2 is greater than r1 and 0 if ranges overlap.
 */
static int __vmranges_cmp(void *r1, void *r2)
{
  struct range_bounds *range1, *range2;

  range1 = (struct range_bounds *)r1;
  range2 = (struct range_bounds *)r2;
  if ((long)(range2->space_start - range1->space_end) < 0) {
    if ((long)(range1->space_start - range2->space_end) >= 0)
      return 1;

    return 0;
  }
  else
    return -1;
}

/* VMR_FIXED and VMR_POPULATE doesn't affect VM ranges merging */
#define __VMR_CLEAR_MASK (VMR_FIXED | VMR_POPULATE)

/*
 * returns true if VM range "vmr" can be merged with another VM range
 * having memory object "memobj" and flags = "flags"
 */
static inline bool can_be_merged(const vmrange_t *vmr,
                                 const memobj_t *memobj, vmrange_flags_t flags)
{
    return (((vmr->flags & ~__VMR_CLEAR_MASK) == (flags & ~__VMR_CLEAR_MASK))
          && ((vmr->memobj == memobj)));
}

/*
 * Create new VM range owned by "parent_vmm".
 * New range will be attached to memory object "memobj".
 * Its space_start will be equal to "va_start",
 * and space_end will be equal to va_start + (npages << PAGE_WIDTH).
 */
static vmrange_t *create_vmrange(vmm_t *parent_vmm, memobj_t *memobj,
                                 uintptr_t va_start, page_idx_t npages,
                                 pgoff_t offset, vmrange_flags_t flags)
{
  vmrange_t *vmr;

  vmr = alloc_from_memcache(__vmrs_cache, 0);
  if (!vmr)
    return NULL;

  vmr->parent_vmm = parent_vmm;
  vmr->flags = flags;
  vmr->bounds.space_start = va_start;
  vmr->bounds.space_end = va_start + (npages << PAGE_WIDTH);
  vmr->memobj = memobj;
  vmr->hole_size = 0;
  vmr->offset = offset;

  parent_vmm->num_vmrs++;
  pin_memobj(memobj);

  return vmr;
}

static void destroy_vmrange(vmrange_t *vmr)
{
  unpin_memobj(vmr->memobj);
  vmr->parent_vmm->num_vmrs--;
  memfree(vmr);
}

/*
 * Merge two VM ranges "prev_vmr" and "next_vmr".
 * VM ranges must be mergeable, prev_vmr->bounds.space_end must be equal to
 * next_vmr->bounds.space_start and offset of next_vmr must strictly follow
 * the offset of prev_vmr.
 */
static vmrange_t *merge_vmranges(vmrange_t *prev_vmr, vmrange_t *next_vmr)
{
  ASSERT_DBG(prev_vmr->parent_vmm == next_vmr->parent_vmm);
  ASSERT_DBG(can_be_merged(prev_vmr, next_vmr->memobj, next_vmr->flags));
  ASSERT_DBG(prev_vmr->bounds.space_end == next_vmr->bounds.space_start);
  ASSERT_DBG(addr2pgoff(prev_vmr, prev_vmr->bounds.space_end)
             == next_vmr->offset);

  VMM_VERBOSE("%s: Merge two VM ranges [%p, %p) and [%p, %p).\n",
              vmm_get_name_dbg(prev_vmr->parent_vmm),
              prev_vmr->bounds.space_start, prev_vmr->bounds.space_end,
              next_vmr->bounds.space_start, next_vmr->bounds.space_end);

  ttree_delete(&prev_vmr->parent_vmm->vmranges_tree, &next_vmr->bounds);
  prev_vmr->bounds.space_end = next_vmr->bounds.space_end;
  prev_vmr->hole_size = next_vmr->hole_size;
  destroy_vmrange(next_vmr);

  return prev_vmr;
}

/*
 * Split VM range "vmrange" to two unique ranges
 * by diapason [va_from, va_to). Return newly created VM
 * range started from va_to and ended with vmrange->bounds.space_end.
 */
static vmrange_t *split_vmrange(vmrange_t *vmrange,
                                uintptr_t va_from, uintptr_t va_to)
{
  vmrange_t *new_vmr;
  page_idx_t npages;

  ASSERT_DBG(!(va_from & PAGE_MASK));
  ASSERT_DBG(!(va_to & PAGE_MASK));
  ASSERT_DBG((va_from > vmrange->bounds.space_start) &&
             (va_to < vmrange->bounds.space_end));

  /*
   * vmrange will be splitted to two unique ranges: both will have the same
   * memory objects and flags. The first one(new) will lay from
   * vmrange->bounds.space_start to va_from exclusively and the second
   * one will lay from va_to to vmrange->bounds.space_end exclusively.
   */
  npages = (vmrange->bounds.space_end - va_to) >> PAGE_WIDTH;
  new_vmr = create_vmrange(vmrange->parent_vmm, vmrange->memobj, va_to, npages,
                           addr2pgoff(vmrange, va_to), vmrange->flags);
  if (!new_vmr)
    return NULL;

  VMM_VERBOSE("%s: Split VM range [%p, %p) to [%p, %p) and [%p, %p)\n",
              vmm_get_name_dbg(vmrange->parent_vmm),
              vmrange->bounds.space_start,
              vmrange->bounds.space_end, vmrange->bounds.space_start,
              va_from, va_to, vmrange->bounds.space_end);

  vmrange->bounds.space_end = va_from;

  /* fix holes size regarding the changes */
  new_vmr->hole_size = vmrange->hole_size;
  vmrange->hole_size = new_vmr->bounds.space_start - vmrange->bounds.space_end;

  /* and insert new VM range into the vmranges_tree. */
  ttree_insert(&vmrange->parent_vmm->vmranges_tree, new_vmr);

  return new_vmr;
}

/* Fix VM range holes size after new VM range is inserted */
static void fix_vmrange_holes_after_insertion(vmm_t *vmm, vmrange_t *vmrange,
                                              ttree_cursor_t *cursor)
{
  vmrange_t *vmr;
  ttree_cursor_t csr;

  ttree_cursor_copy(&csr, cursor);
  if (!ttree_cursor_prev(&csr)) {
    vmr = ttree_item_from_cursor(&csr);
    VMM_VERBOSE("%s(P) [%p, %p): old hole size: %ld, new hole size: %ld\n",
                vmm_get_name_dbg(vmm), vmr->bounds.space_start,
                vmr->bounds.space_end, vmr->hole_size,
                vmrange->bounds.space_start - vmr->bounds.space_end);

    vmr->hole_size = vmrange->bounds.space_start - vmr->bounds.space_end;
  }

  ttree_cursor_copy(&csr, cursor);
  if (!ttree_cursor_next(&csr)) {
    vmr = ttree_item_from_cursor(&csr);

    VMM_VERBOSE("%s(N) [%p, %p): old hole size: %ld, new hole size: %ld\n",
                vmm_get_name_dbg(vmm), vmrange->bounds.space_start,
                vmrange->bounds.space_end, vmrange->hole_size,
                vmr->bounds.space_start - vmrange->bounds.space_end);

    vmrange->hole_size = vmr->bounds.space_start - vmrange->bounds.space_end;
  }
  else {
    VMM_VERBOSE("%s(L) [%p, %p): old hole size: %ld, new hole size: %ld\n",
                vmm_get_name_dbg(vmm), vmrange->bounds.space_start,
                vmrange->bounds.space_end, vmrange->hole_size,
                USPACE_VADDR_TOP - vmrange->bounds.space_end);

    vmrange->hole_size = USPACE_VADDR_TOP - vmrange->bounds.space_end;
  }
}

void vm_mandmap_register(vm_mandmap_t *mandmap, const char *mandmap_name)
{
#ifndef CONFIG_TEST
  ASSERT(!valid_user_address_range(mandmap->virt_addr,
                                   mandmap->num_pages << PAGE_WIDTH));
#endif /* CONFIG_TEST */

  mandmap->name = (char *)mandmap_name;
  mandmap->flags &= KMAP_FLAGS_MASK;
  list_add2tail(&__mandmaps_lst, &mandmap->node);
  __num_mandmaps++;
}

int vm_mandmaps_roll(vmm_t *target_mm)
{
  int status = 0;
  vm_mandmap_t *mandmap;
  page_idx_t pidx;
  uintptr_t va, va_to;

  list_for_each_entry(&__mandmaps_lst, mandmap, node) {
    va_to = mandmap->virt_addr +
      ((uintptr_t)mandmap->num_pages << PAGE_WIDTH);
    pidx = mandmap->phys_addr >> PAGE_WIDTH;
    va = mandmap->virt_addr;
    while (va < va_to) {
      status = mmap_page(&target_mm->rpd, va, pidx, mandmap->flags);
      if (status) {
        VMM_VERBOSE("[%s] Failed to map page %#x to %p in "
                    "mandatory mapping %s! [RET = %d]\n",
                    vmm_get_name_dbg(target_mm),
                    pidx, va, mandmap->name, status);
        goto out;
      }

      if (likely(page_idx_is_present(pidx)))
        pin_page_frame(pframe_by_id(pidx));

      va += PAGE_SIZE;
      pidx++;
    }

    VMM_VERBOSE("[%s]: Mandatory mapping \"%s\" [%p -> %p] was initialized\n",
                vmm_get_name_dbg(target_mm), mandmap->name, mandmap->virt_addr,
                mandmap->virt_addr + (mandmap->num_pages << PAGE_WIDTH));
  }

out:
  return status;
}

void vmm_subsystem_initialize(void)
{
  kprintf("[MM] Initializing VMM subsystem...\n");
  __vmms_cache = create_memcache("VMM objects cache", sizeof(vmm_t), 1,
                                 MMPOOL_KERN | SMCF_IMMORTAL |
                                 SMCF_UNIQUE | SMCF_LAZY);
  if (!__vmms_cache)
    panic("vmm_subsystem_initialize: Can not create memory "
          "cache for VMM objects. ENOMEM");

  __vmrs_cache = create_memcache("Vmrange objects cache", sizeof(vmrange_t), 1,
                                 MMPOOL_KERN | SMCF_IMMORTAL |
                                 SMCF_UNIQUE | SMCF_LAZY);
  if (!__vmrs_cache)
    panic("vmm_subsystem_initialize: Can not create memory "
          "cache for vmrange objects. ENOMEM.");
}

vmm_t *vmm_create(task_t *owner)
{
  vmm_t *vmm;
  
  vmm = alloc_from_memcache(__vmms_cache, 0);
  if (!vmm)
    return NULL;

  memset(vmm, 0, sizeof(*vmm));
  ttree_init(&vmm->vmranges_tree, __vmranges_cmp, vmrange_t, bounds);
  rwsem_initialize(&vmm->rwsem);
  if (initialize_rpd(&vmm->rpd, vmm) < 0) {
    memfree(vmm);
    vmm = NULL;
  }

  vmm->owner = owner;
  vmm_set_name_from_pid_dbg(vmm);
  return vmm;
}

/*
 * Remove all VM ranges and munmap all pages they have
 * from the VM ranges tree of given VMM structure.
 */
void __clear_vmranges_tree(vmm_t *vmm)
{
  ttree_node_t *tnode;
  int i;
  vmrange_t *vmr;
  memobj_t *memobj;

  tnode = ttree_tnode_leftmost(vmm->vmranges_tree.root);
  if (!tnode) {
    ASSERT(vmm->num_vmrs == 0);
    return;
  }

  while (tnode) {
    tnode_for_each_index(tnode, i) {
      vmr = ttree_key2item(&vmm->vmranges_tree, tnode_key(tnode, i));
      memobj = vmr->memobj;

      /*
       * clear_range memory object method must be called explicitly
       * (I mean not via memobj_method_call): because here we don't care
       * was memory object marked for delete or not.
       */
      memobj->mops->depopulate_pages(vmr, vmr->bounds.space_start,
                                     vmr->bounds.space_end);
      destroy_vmrange(vmr);
    }

    tnode = tnode->successor;
  }

  ASSERT(vmm->num_vmrs == 0);
  ttree_destroy(&vmm->vmranges_tree);
}

void vmm_destroy(vmm_t *vmm)
{
  VMM_VERBOSE("[%s]: Destroying VMM...\n", vmm_get_name_dbg(vmm));
  rwsem_down_write(&vmm->rwsem);
  __clear_vmranges_tree(vmm);
  VMM_VERBOSE("[%s]: VM ranges tree was successfully "
              "destroyed.\n", vmm_get_name_dbg(vmm));

  /* TODO DK: implement cleanup of sticky memory objects if exist */
  rwsem_up_write(&vmm->rwsem);
  memfree(vmm);
}

/*
 * Clone all VM ranges in "src" to "dst" and map them into dst's root
 * page directory regarding the policy specified by the "flags".
 * FIXME DK: dude, what about VM ranges with VMR_CANRECPAGES? 
 */
int vmm_clone(vmm_t *dst, vmm_t *src, int flags)
{
  ttree_node_t *tnode;
  int i = 0, ret = 0;
  vmrange_t *vmr, *new_vmr;
  uintptr_t addr;
  page_idx_t pidx;
  vmrange_flags_t mmap_flags;
  page_frame_t *page;

  ASSERT(dst != src);
  ASSERT((flags & (VMM_CLONE_POPULATE | VMM_CLONE_COW))
         != (VMM_CLONE_POPULATE | VMM_CLONE_COW));

  /*
   * It's important to lock src's semaphore on *write*, because
   * it prevents other tasks sharing the same address space from
   * changing content of VM ranges they own.
   */
  rwsem_down_write(&src->rwsem);
  tnode = ttree_tnode_leftmost(src->vmranges_tree.root);
  ASSERT(tnode != NULL);

  /*
   * Browse throug each VM range in src and
   * copy its content into the dst's VM ranges tree
   */
  while (tnode) {
    tnode_for_each_index(tnode, i) {
      vmr = ttree_key2item(&src->vmranges_tree, tnode_key(tnode, i));
      mmap_flags = vmr->flags;
      if (((vmr->flags & VMR_PHYS) && !(flags & VMM_CLONE_PHYS)) ||
               (vmr->flags & VMR_GATE)) {
        continue;
      }

      /*
       * If current VM range is shared and VMM_CLONE_SHARED
       * wasn't specified, pages in src's VM range would be copied
       * to the dst's address space. To do so properly
       * we have to change VMR_SHARED to VMR_PRIVATE.
       */
      if ((vmr->flags & VMR_SHARED) && !(flags & VMM_CLONE_SHARED)) {
        mmap_flags &= ~VMR_SHARED;
        mmap_flags |= VMR_PRIVATE;
      }

      /* Cloned VM ranges wouoldn't inherite VMR_CANRECPAGES flag. */
      if (vmr->flags & VMR_CANRECPAGES) {
        mmap_flags &= ~VMR_CANRECPAGES;
      }

      new_vmr = create_vmrange(dst, vmr->memobj, vmr->bounds.space_start,
                               (vmr->bounds.space_end -
                                vmr->bounds.space_start) >> PAGE_WIDTH,
                               vmr->offset, mmap_flags);
      if (!new_vmr) {
        VMM_VERBOSE("[%s] Failed to create a clone of VM "
                    "range [%p, %p). ENOMEM.\n", vmm_get_name_dbg(dst),
                    vmr->bounds.space_start, vmr->bounds.space_end);

        ret = -ENOMEM;
        goto clone_failed;
      }

      new_vmr->hole_size = vmr->hole_size;
      ASSERT(!ttree_insert(&dst->vmranges_tree, new_vmr));

      /*
       * Handle each mapped page in given range
       * regarding the specified clone policy
       */
      for (addr = vmr->bounds.space_start;
           addr < vmr->bounds.space_end; addr += PAGE_SIZE) {

        RPD_LOCK_READ(&src->rpd);
        pidx = vaddr_to_pidx(&src->rpd, addr);        
        if (pidx == PAGE_IDX_INVAL) {
          RPD_UNLOCK_READ(&src->rpd);
          continue;
        }

        RPD_UNLOCK_READ(&src->rpd);
        if (((flags & VMM_CLONE_COW) && !(new_vmr->flags & VMR_PHYS))) {
          /*
           * If copy-on-write clone policy was specified all pages in
           * a given range will be marked with PF_COW flag and will
           * be remapped in src's address space with "read-only" rights.
           */

          page = pframe_by_id(pidx);
          if ((vmr->flags & (VMR_WRITE | VMR_PRIVATE)) ==
              (VMR_WRITE | VMR_PRIVATE)) {
            mmap_flags &= ~VMR_WRITE;
          }
          if (!(page->flags & PF_COW)) {
            RPD_LOCK_WRITE(&src->rpd);
            ret = memobj_method_call(vmr->memobj, delete_page, vmr, page);

            if (ret) {
              VMM_VERBOSE("[%s] Failed to delete page %#x from range [%p, %p) "
                          "(address %p). [ERR = %d]\n", vmm_get_name_dbg(src),
                          pframe_number(page), vmr->bounds.space_start,
                          vmr->bounds.space_end, addr, ret);
              RPD_UNLOCK_WRITE(&src->rpd);
              goto clone_failed;
            }

            page->flags |= PF_COW;
            ret = memobj_method_call(vmr->memobj, insert_page, vmr, page,
                                     addr, mmap_flags);
            if (ret) {
              VMM_VERBOSE("[%s] Failed to remap page %#x to address %p in "
                          "VM range [%p, %p). [ERR = %d]\n",
                          vmm_get_name_dbg(src), pframe_number(page), addr,
                          vmr->bounds.space_start, vmr->bounds.space_end, ret);
              RPD_UNLOCK_WRITE(&src->rpd);
              goto clone_failed;
            }

            RPD_UNLOCK_WRITE(&src->rpd);
          }
        }
        else if ((flags & VMM_CLONE_POPULATE) &&
            ((((vmr->flags & VMR_SHARED) && !(flags & VMM_CLONE_SHARED)))
             || (vmr->flags & VMR_WRITE))) {
          page_frame_t *new_page = alloc_page(MMPOOL_USER | AF_ZERO);

          if (!new_page) {
            VMM_VERBOSE("[%s] Failed to allocate new page for copying content "
                        "of page %#x. ENOMEM.\n", vmm_get_name_dbg(dst), pidx);

            ret = -ENOMEM;
            goto clone_failed;
          }

          page = pframe_by_id(pidx);
          copy_page_frame(new_page, page);
          new_page->offset = page->offset;
          pidx = pframe_number(new_page);
        }

        page = pframe_by_id(pidx);
        pin_page_frame(page);

        /*
         * We don't need to lock address space of destination VMM
         * because on the moment we're cloning source's address space
         * destination VMM has not any working threads.
         */
        ret = memobj_method_call(new_vmr->memobj, insert_page, new_vmr,
                                 page, addr, mmap_flags);
        if (ret) {
          VMM_VERBOSE("[%s] Failed to mmap page %#x to address %p "
                      "during VMM cloning. [ERR = %d]\n",
                      vmm_get_name_dbg(dst), pidx, addr, ret);
          unpin_page_frame(page);
          goto clone_failed;
        }
      }
    }

    tnode = tnode->successor;
  }


  rwsem_up_write(&src->rwsem);
  return ret;

clone_failed:
  rwsem_up_write(&src->rwsem);
  __clear_vmranges_tree(dst);

  return ret;
}

/* Find a room for VM range with length "length". */
uintptr_t find_free_vmrange(vmm_t *vmm, uintptr_t length,
                            ttree_cursor_t *cursor)
{
  uintptr_t start = USPACE_VADDR_BOTTOM;
  vmrange_t *vmr;
  ttree_node_t *tnode = NULL;
  int i = 0;

  tnode = ttree_tnode_leftmost(vmm->vmranges_tree.root);
  if (unlikely(tnode == NULL))
    goto found;

  /*
   * At first check if there is enough space between
   * the start of the very first vmrange and the bottom
   * user-space virtual address. If so, we're really lucky guys ;)
   */
  vmr = ttree_key2item(&vmm->vmranges_tree, tnode_key_min(tnode));
  if ((start - vmr->bounds.space_start) >= length) {
    i = tnode->min_idx;
    goto found;
  }

  /*
   * Otherwise browse through all VM ranges in the tree starting from the
   * lefmost range until a big enough room is found.
   */
  while (tnode) {
    tnode_for_each_index(tnode, i) {
      vmr = ttree_key2item(&vmm->vmranges_tree, tnode_key(tnode, i));
      if (vmr->hole_size >= length) {
        start = vmr->bounds.space_end;
        goto found;
      }
    }

    tnode = tnode->successor;
  }

  VMM_VERBOSE("%s: Failed to find free VM range with size = %d pages\n",
              vmm_get_name_dbg(vmm), length);
  return INVALID_ADDRESS; /* Woops, nothing was found. */

found:
  if (cursor) {
    ttree_cursor_init(&vmm->vmranges_tree, cursor);
    if (tnode) {
      /*
       * It's quite important to initialize T*-tree cursor porperly:
       * Because of semantics of T*-tree, the cursor item will be inserted
       * by, must point to the next position after the one that was found.
       * Moreover, the cursor must be in pending state.
       */

      cursor->tnode = tnode;
      cursor->idx = i;
      cursor->side = TNODE_BOUND;
      cursor->state = TT_CSR_TIED;
      ttree_cursor_next(cursor);
      cursor->state = TT_CSR_PENDING;
    }
  }

  return start;
}

vmrange_t *vmrange_find(vmm_t *vmm, uintptr_t va_start,
                        uintptr_t va_end, ttree_cursor_t *cursor)
{
  struct range_bounds bounds;
  vmrange_t *vmr;

  bounds.space_start = va_start;
  bounds.space_end = va_end;
  vmr = ttree_lookup(&vmm->vmranges_tree, &bounds, cursor);
  return vmr;
}

/*
 * Find all VM ranges in diapason [va_from, va_to).
 * It's a lazy function. Every next item is yielded by vmrange_set_t on demand.
 */
void vmranges_find_covered(vmm_t *vmm, uintptr_t va_from,
                           uintptr_t va_to, vmrange_set_t *vmrs)
{
  ASSERT(!(va_from & PAGE_MASK) && !(va_to & PAGE_MASK));
  memset(vmrs, 0, sizeof(*vmrs));
  vmrs->va_from = va_from;
  vmrs->va_to = va_to;
  ttree_cursor_init(&vmm->vmranges_tree, &vmrs->cursor);
  vmrs->vmr = vmrange_find(vmm, va_from, va_from + 1, &vmrs->cursor);
  if (!vmrs->vmr) {
    if (!ttree_cursor_next(&vmrs->cursor)) {
      vmrs->vmr = ttree_item_from_cursor(&vmrs->cursor);
      if (vmrs->vmr->bounds.space_end > va_to)
        vmrs->vmr = NULL;
    }
  }
}

long vmrange_map(memobj_t *memobj, vmm_t *vmm, uintptr_t addr,
                 page_idx_t npages, vmrange_flags_t flags, pgoff_t offset)
{
  vmrange_t *vmr;
  ttree_cursor_t cursor, csr_tmp;
  int err = 0;
  bool was_merged = false;

  vmr = NULL;
  ASSERT_DBG(memobj != NULL);
  ttree_cursor_init(&vmm->vmranges_tree, &cursor);

  /* general checking */
  if (!(flags & VMR_PROT_MASK) /* protocol must be set */
      /* private *or* shared flags must be set */
      || !(flags & (VMR_PRIVATE | VMR_SHARED))
      /* but they mustn't be set together */
      || ((flags & (VMR_PRIVATE | VMR_SHARED)) == (VMR_PRIVATE | VMR_SHARED))
      /* npages must not be zero */
      || !npages
      /* VMR_NONE must not be set with VMR_SHARED or VMR_PRIVATE */
      || ((flags & VMR_NONE) && ((flags & VMR_PROT_MASK) != VMR_NONE))
      /* VMR_SHARED can not coexist with VMR_PHYS */
      || (flags & (VMR_PHYS | VMR_SHARED)) == (VMR_PHYS | VMR_SHARED)
      /* VMR_FIXED expects that address is specified */
      || ((flags & VMR_FIXED) && !addr)
      /* VM ranges that are able to receive pages must not be shared, phys or VMR_NONE. */
      || ((flags & VMR_CANRECPAGES) && (flags & (VMR_PHYS | VMR_NONE | VMR_SHARED))))
  {
    err = -EINVAL;
    goto err;
  }

  /*
   * If corresponding memory object doesn't
   * support shared memory facility, return an error.
   */
  if ((flags & VMR_SHARED) && !(memobj->flags & MMO_FLG_SHARED)) {
    err = -ENOTSUP;
    goto err;
  }
  if (flags & VMR_PHYS) {
    flags |= VMR_NOCACHE; /* physical pages must not be cached */
    /* and task taht maps them must be trusted */

    /* only generic memory object supports physical mappings */
    if (memobj->id != GENERIC_MEMOBJ_ID) {
      err = -ENOTSUP;
      goto err;
    }
  }
  if (addr) {
    if (!valid_user_address_range(addr, ((uintptr_t)npages << PAGE_WIDTH))) {
      if (flags & VMR_FIXED) {
        err = -ENOMEM;
        goto err;
      }

      goto allocate_address;
    }

    vmr = vmrange_find(vmm, addr, addr + (npages << PAGE_WIDTH), &cursor);
    if (vmr) {
      if (flags & VMR_FIXED) {
        VMM_VERBOSE("%s: Failed to allocate fixed VM range [%p, %p) "
                    "because it is covered by this one: [%p, %p)\n",
                    vmm_get_name_dbg(vmm), addr, addr + ((uintptr_t)npages << PAGE_WIDTH),
                    vmr->bounds.space_start, vmr->bounds.space_end);
        err = -EINVAL;
        goto err;
      }
    }
    else
      goto create_vmrange;
  }

  /*
   * It has been asked to find suitable portion of address space
   * satisfying requested length.
   */
  allocate_address:
  addr = find_free_vmrange(vmm, (npages << PAGE_WIDTH), &cursor);
  if (addr == INVALID_ADDRESS) {
    err = -ENOMEM;
    goto err;
  }

create_vmrange:
  /*
   * Generic memory object was disigned to collect and handle all anonymous and
   * physical mappings. Unlike other memory objects it actually doesn't
   * have *real*(I mean tied with anything) offset, but it has to have it
   * in order to be semantically compatible with other types of memory objects.
   * If the mapping is anonymous, offset will be equal to page index of the
   * bottom virtual address of VM range. Otherwise(i.e. if
   * mapping is physical), offset is already specified by the user.
   */
  if ((memobj->id == GENERIC_MEMOBJ_ID) && !(flags & VMR_PHYS))
    offset = addr >> PAGE_WIDTH;
  if ((offset + npages) > memobj->size) {
    err = -EOVERFLOW;
    goto err;
  }

  ttree_cursor_copy(&csr_tmp, &cursor);

  /* Try attach new mapping to the top of previous VM range */
  if (!ttree_cursor_prev(&csr_tmp)) {
    vmr = ttree_item_from_cursor(&csr_tmp);
    if (can_be_merged(vmr, memobj, flags) && (vmr->bounds.space_end == addr) &&
        (addr2pgoff(vmr, vmr->bounds.space_end) == offset)) {

      VMM_VERBOSE("%s: Attach [%p, %p) to the top of [%p, %p)\n",
                  vmm_get_name_dbg(vmm), addr, addr + (npages << PAGE_WIDTH),
                  vmr->bounds.space_start, vmr->bounds.space_end);

      vmr->hole_size -= (addr + ((uintptr_t)npages << PAGE_WIDTH)
                         - vmr->bounds.space_end);
      vmr->bounds.space_end = addr + ((uintptr_t)npages << PAGE_WIDTH);
      was_merged = true;
    }
  }

  ttree_cursor_copy(&csr_tmp, &cursor);
  if (!ttree_cursor_next(&csr_tmp)) {
    vmrange_t *prev = vmr;

    vmr = ttree_item_from_cursor(&csr_tmp);

    /* Try attach new mapping to the bottom of the next VM range. */
    if (can_be_merged(vmr, memobj, flags)) {
      if (!was_merged && (vmr->bounds.space_start ==
                          (addr + (npages << PAGE_WIDTH))) &&
          ((offset + npages) == vmr->offset)) {

        VMM_VERBOSE("%s: Attach [%p, %p) to the bottom of [%p, %p)\n",
                    vmm_get_name_dbg(vmm), addr, addr + (npages << PAGE_WIDTH),
                    vmr->bounds.space_start, vmr->bounds.space_end);

        vmr->offset = offset;
        vmr->bounds.space_start = addr;
        was_merged = true;
        if (prev)
          prev->hole_size = vmr->bounds.space_start - prev->bounds.space_end;
      }
      else if (was_merged && (prev->bounds.space_end
                              == vmr->bounds.space_start) &&
               (addr2pgoff(prev, prev->bounds.space_end) == vmr->offset)) {
        /*
         * If the mapping was merged with either previous or next VM ranges,
         * and after it they become mergeable, merge them.
         */
        vmr = merge_vmranges(prev, vmr);
      }
    }
    else {
      vmr = prev;
    }
  }
  if (!was_merged) {
    /*
     * If neither nor previous, nor next mappigns were mergeable,
     * the new VM range has to be allocated.
     */
    vmr = create_vmrange(vmm, memobj, addr, npages, offset, flags);
    if (!vmr) {
      err = -ENOMEM;
      goto err;
    }

    ttree_insert_placeful(&cursor, vmr);
    fix_vmrange_holes_after_insertion(vmm, vmr, &cursor);
  }
  if (flags & (VMR_PHYS | VMR_POPULATE)) {
    err = memobj_method_call(memobj, populate_pages, vmr, addr, npages);
    if (err)
      goto err;
  }

  /*
   * if VMR_STACK flag was specified, the returned address must be
   * the top address of the VM range.
   */
  return (!(flags & VMR_STACK) ? addr : (addr + (npages << PAGE_WIDTH)));

err:
  if (vmr) {
    ttree_delete_placeful(&cursor);
    destroy_vmrange(vmr);
  }

  return err;
}

/* Munmap all VM ranges starting from va_from continuing for npages pages. */
int unmap_vmranges(vmm_t *vmm, uintptr_t va_from, page_idx_t npages)
{
  ttree_cursor_t cursor;
  uintptr_t va_to = va_from + ((uintptr_t)npages << PAGE_WIDTH);
  vmrange_t *vmr, *vmr_prev = NULL;
  
  ASSERT_DBG(!(va_from & PAGE_MASK));
  ttree_cursor_init(&vmm->vmranges_tree, &cursor);

  /*
   * At first try to find a diapason containing range
   * [va_from, va_from + PAGE_SIZE), i.e. minimum starting range.
   * If it wasn't found, try get the next one in the tree
   * using T*-tree cursor. Due its nature it will return the next
   * item in the tree(if exists) after position where
   * [va_from, va_from + PAGE_SIZE) could be. If such item is found and
   * if it has appropriate bounds, it is the very first VM range
   * covered by given diapason.
   */
  vmr = vmrange_find(vmm, va_from, va_from + 1, &cursor);
  if (!vmr) {
    if (!ttree_cursor_next(&cursor))
      vmr = ttree_item_from_cursor(&cursor);
    else
      return 0;
  }

  /*
   * If munmapped range is fully covered by a given one,
   * we have to split it.
   */
  if ((vmr->bounds.space_start < va_from) &&
      (vmr->bounds.space_end > va_to)) {

    memobj_method_call(vmr->memobj, depopulate_pages, vmr,
                       va_from, va_to);
    
    if (!split_vmrange(vmr, va_from, va_to)) {
      return ERR(-ENOMEM);
    }

    return 0; /* done */
  }
  else if (va_from > vmr->bounds.space_start) {
    uintptr_t new_end = va_from;

    /*
     * va_from may be greater than the bottom virtual address of
     * given range. If so, range's top address must be decreased
     * regarding the new top address.
     */
    memobj_method_call(vmr->memobj, depopulate_pages, vmr,
                       va_from, va_from + (vmr->bounds.space_end - va_from));
    va_from = vmr->bounds.space_end;
    vmr->bounds.space_end = new_end;

    /* Remember this VM range as a previous. Later we'll fix its hole size. */
    vmr_prev = vmr;
    if (ttree_cursor_next(&cursor) < 0) {
      vmr = NULL;
      goto out;
    }

    vmr = ttree_item_from_cursor(&cursor);
  }
  else { /* previous VM range isn't defined */
    ttree_cursor_t csr_prev;

    ttree_cursor_copy(&csr_prev, &cursor);
    if (!ttree_cursor_prev(&csr_prev))
      vmr_prev = ttree_item_from_cursor(&csr_prev);
  }
  for (;;) {
    if (va_from >= va_to)
      break;

    /*
     * va_to could be less than the top address of given VM range.
     * This means that
     * a) Given VM range is the last one
     * b) The range's bottom virtual address must be decreased and become
     *    equal to va_to value.
     */
    if (unlikely(va_to < vmr->bounds.space_end)) {
      vmr->offset = addr2pgoff(vmr, va_to);
      vmr->bounds.space_start = va_to;
      memobj_method_call(vmr->memobj, depopulate_pages, vmr,
                         va_from, va_from + (va_to - va_from));
      break;
    }

    memobj_method_call(vmr->memobj, depopulate_pages, vmr,
                       va_from, vmr->bounds.space_end);
    ttree_delete_placeful(&cursor);
    destroy_vmrange(vmr);
    vmr = NULL;
    if (cursor.state != TT_CSR_TIED) {
      break;
    }

    /*
     * Due the nature of T*-tree, after deletion by the cursor is done,
     * the cursor points to the next item in a tree(if exists). So we can
     * safely fetch an item from the cursor(cursor validation was done above).
     */
    vmr = ttree_item_from_cursor(&cursor);
    va_from = vmr->bounds.space_start;
  }

  out:
  if (!vmr && (cursor.state == TT_CSR_TIED)) {
    /*
     * If the next VM range(next after just removed) is not defined,
     * try to fetch it out.
     */
    vmr = ttree_item_from_cursor(&cursor);
  }
  if (likely(vmr_prev != NULL)) {
    /* vmr_prev is highest VM range in an address space */
    if (unlikely(vmr == NULL)) {
      vmr_prev->hole_size = USPACE_VADDR_TOP - vmr_prev->bounds.space_end;
    }
    else {
      vmr_prev->hole_size = vmr->bounds.space_start -
        vmr_prev->bounds.space_end;
    }
  }

  return 0;
}

int __fault_in_user_page(vmrange_t *vmrange, uintptr_t addr,
                         uint32_t pfmask, page_idx_t *out_pidx)
{
  pde_t *pde;
  vmm_t *vmm = vmrange->parent_vmm;
  page_idx_t pidx;
  vmrange_flags_t vmr_mask = VMR_READ;
  int ret;

  ASSERT_DBG((addr >= vmrange->bounds.space_start)
             && (addr < vmrange->bounds.space_end));
  ASSERT_DBG(__valid_vmr_rights(vmrange, pfmask));
  ASSERT_DBG((vmrange->flags & vmr_mask) == vmr_mask);

  if (pfmask & PFLT_WRITE)
    vmr_mask |= VMR_WRITE;

  RPD_LOCK_READ(&vmm->rpd);
  pidx = __vaddr_to_pidx(&vmm->rpd, addr, &pde);
  if (pidx != PAGE_IDX_INVAL) {
    /*
     * If page by given address is already mapped with valid
     * protection attributes, we don't need emulate PF on it.
     */
    if ((ptable_to_kmap_flags(pde_get_flags(pde)) & vmr_mask) == vmr_mask) {
     RPD_UNLOCK_READ(&vmm->rpd);
      goto out;
    }
  }
  else
    pfmask |= PFLT_NOT_PRESENT;

  RPD_UNLOCK_READ(&vmm->rpd);
  ret = memobj_method_call(vmrange->memobj, handle_page_fault,
                           vmrange, addr, pfmask);
  if (ret)
    return ret;

  /*
   * After fault is handled and tied with VM range memory object
   * doesn't return an error, page index of mapped page may be easily
   * fetched from the page table of the proccess. Note, here we don't need to
   * lock the table: fault_in_user_pages function is called with downed on read
   * vmm semaphore, so after fault is handled and page is present in the table,
   * it can not be unmapped while semaphore is downed/
   */
  pidx = __vaddr_to_pidx(&vmm->rpd, addr, &pde);
  ASSERT(pidx != PAGE_IDX_INVAL);

out:
  if (out_pidx)
    *out_pidx = pidx;

  return 0;
}

int fault_in_user_pages(vmm_t *vmm, uintptr_t address, size_t length, uint32_t pfmask,
                        void (*callback)(vmrange_t *vmr, page_frame_t *page, void *data), void *data)
{
  page_idx_t pidx;
  int ret = 0;
  uintptr_t va;
  pgoff_t npages, i = 0;
  vmrange_t *vmr;
  ttree_cursor_t cursor;

  va = PAGE_ALIGN(address + length);
  npages = (va - PAGE_ALIGN_DOWN(address)) >> PAGE_WIDTH;
  va = PAGE_ALIGN_DOWN(address);

  /* don't fuck with me */
  if (!valid_user_address_range(va, (npages << PAGE_WIDTH))) {
    return ERR(-EFAULT);
  }

  vmr = vmrange_find(vmm, va, va + 1, &cursor);
  if (!vmr) {
    return ERR(-EFAULT);
  }

  while (i < npages) {
    /*
     * If va crosses the end of current VM range, try get the next VM range
     * from the T*-tree cursor. If va fits in next VM range's bounds, iteration
     * can be continued.
     */
    if (unlikely(va >= vmr->bounds.space_end)) {
      if (ttree_cursor_next(&cursor) < 0) {
        return ERR(-EFAULT);
      }
    }

    vmr = ttree_item_from_cursor(&cursor);
    if ((va < vmr->bounds.space_start) ||
        !__valid_vmr_rights(vmr, pfmask)) {
      return ERR(-EFAULT);
    }

    ret = __fault_in_user_page(vmr, va, pfmask, &pidx);
    if (ret) {
      return ERR(ret);
    }

    if (callback) {
      callback(vmr, pframe_by_id(pidx), data);
    }

    va += PAGE_SIZE;
    i++;
  }

  return 0;
}

int vmm_handle_page_fault(vmm_t *vmm, uintptr_t fault_addr, uint32_t pfmask)
{
  int ret;
  vmrange_t *vmr;
  memobj_t *memobj;

  rwsem_down_read(&vmm->rwsem);
  vmr = vmrange_find(vmm, PAGE_ALIGN_DOWN(fault_addr),
                     PAGE_ALIGN_DOWN(fault_addr) + 1, NULL);
  if (!vmr) {
    ret = -EFAULT;
    goto out;
  }

  /*
   * If fault was caused by write attemption and VM range that was found
   * has VMR_WRITE flag unset or VMR_NONE flag set, fault can not be handled.
   */
  if (!__valid_vmr_rights(vmr, pfmask)) {
    ret = -EFAULT;
    goto out;
  }

  ASSERT(vmr->memobj != NULL);
  memobj = vmr->memobj;
  ret = memobj_method_call(memobj, handle_page_fault, vmr,
                           PAGE_ALIGN_DOWN(fault_addr), pfmask);
out:
  rwsem_up_read(&vmm->rwsem);
  return ERR(ret);
}

#define MMAP_SIZE_LIMIT MB2B(1024UL)

long sys_mmap(pid_t victim, memobj_id_t memobj_id, struct mmap_args *uargs)
{
  task_t *victim_task = NULL;
  memobj_t *memobj = NULL;
  vmm_t *vmm;
  long ret;
  struct mmap_args margs;
  vmrange_flags_t vmrflags;

  if (likely(!victim)) {
    victim_task = current_task();
  }
  else {
    victim_task = pid_to_task(victim);
    if (!victim_task)
      return -ESRCH;
  }

  vmm = victim_task->task_mm;
  if (copy_from_user(&margs, uargs, sizeof(margs))) {
    ret = -EFAULT;
    goto out;
  }
  if (unlikely(PAGE_ALIGN(margs.size) >= MMAP_SIZE_LIMIT)) {
    ret = -E2BIG;
    goto out;
  }

  vmrflags = (margs.prot & VMR_PROT_MASK) | (margs.flags << VMR_FLAGS_OFFS);
  /*
   * Basic rules:
   * 1) offset must be page aligned
   * 2) if VMR_FIXED is set, address must be page aligned
   * 3) size mustn't be zero.
   */
  if ((margs.offset & PAGE_MASK)
      || ((vmrflags & VMR_FIXED) && (margs.addr & PAGE_MASK))
      || !margs.size) {
    ret = -EINVAL;
    goto out;
  }
  if ((vmrflags & VMR_ANON) || (memobj_id == GENERIC_MEMOBJ_ID)) {
    memobj = memobj_pin_by_id(GENERIC_MEMOBJ_ID);
    ASSERT(memobj != NULL);
  }
  else {
    memobj = memobj_pin_by_id(memobj_id);
    if (!memobj) {
      ret = -ENOENT;
      goto out;
    }
  }

  rwsem_down_write(&vmm->rwsem);
  ret = vmrange_map(memobj, vmm, margs.addr, PAGE_ALIGN(margs.size) >> PAGE_WIDTH,
                    vmrflags, PAGE_ALIGN(margs.offset) >> PAGE_WIDTH);
  rwsem_up_write(&vmm->rwsem);

out:
  if (victim_task && victim)
    release_task_struct(victim_task);
  if (memobj)
    unpin_memobj(memobj);

  return ret;
}

int sys_munmap(pid_t victim, uintptr_t addr, size_t length)
{
  task_t *victim_task = NULL;
  vmm_t *vmm;
  int ret;

  if (likely(!victim))
    victim_task = current_task();
  else {
    victim_task = pid_to_task(victim);
    if (!victim_task) {
      return ERR(-ESRCH);
    }
  }

  vmm = victim_task->task_mm;

  /* address must be page aligned and length must be specified */
  if (!length || (addr & PAGE_MASK)) {
    ret = -EINVAL;
    goto out;
  }

  length = PAGE_ALIGN(length);
  if (!valid_user_address_range(addr, length)) {
    ret = -EINVAL;
    goto out;
  }

  rwsem_down_write(&vmm->rwsem);
  ret = unmap_vmranges(vmm, addr, length >> PAGE_WIDTH);
  rwsem_up_write(&vmm->rwsem);

out:
  if (victim_task && victim)
    release_task_struct(victim_task);

  return ERR(ret);
}

/* VM ranges with the followin flags are not allowed to grant pages */
#define NON_GRANTABLE_VMRS_MASK (VMR_PHYS | VMR_SHARED | VMR_GATE | VMR_NONE)

int sys_grant_pages(uintptr_t va_from, size_t length,
                    pid_t target_pid, uintptr_t target_addr)
{
  task_t *target = NULL, *current;
  vmrange_t *vmr, *target_vmr;
  ttree_cursor_t cursor;
  page_idx_t npages, i, pidx;
  page_frame_t *page;
  int ret = 0;

  /* TODO DK: add rights checking. */
  current = current_task();
  if ((va_from & PAGE_MASK) || !length || (target_addr & PAGE_MASK) ||
      !valid_user_address_range(va_from, length) ||
      (current->pid == target_pid)) {
    return -EINVAL;
  }

  /* Whant to grant pages to idle task? fuck you! */
  if (!target_pid)
    return -EPERM;

  length = PAGE_ALIGN(length);
  npages = length >> PAGE_WIDTH;
  target = pid_to_task(target_pid);
  if (!target)
    return -ESRCH;

  rwsem_down_read(&target->task_mm->rwsem);
  /* Find target VM range */
  target_vmr = vmrange_find(target->task_mm, target_addr,
                            target_addr + length - 1, NULL);
  if (!target_vmr) {
    ret = -ENOMEM;
    goto unlock_target;
  }

  /* VMR_CANRECPAGES implies that both VMR_PHYS and VMR_SHARED are not set */
  if (!(target_vmr->flags & VMR_CANRECPAGES)) {
    ret = -EACCES;
    goto unlock_target;
  }
  if ((target_addr < target_vmr->bounds.space_start) ||
      ((target_addr + length) > target_vmr->bounds.space_end)) {
    ret = -ENOMEM;
    goto unlock_target;
  }

  /* Ok, now we can take care about pages we want to grant */
  rwsem_down_read(&current->task_mm->rwsem);
  /* Find first range pages will be granted from */
  vmr = vmrange_find(current->task_mm, va_from, va_from + 1, &cursor);
  if (!vmr) {
    ret = -ENOMEM;
    goto unlock_current;
  }
  /* Only serveral types of VM ranges can grant pages they have */
  if (vmr->flags & NON_GRANTABLE_VMRS_MASK) {
    ret = -EACCES;
    goto unlock_current;
  }

  for (i = 0; i < npages; i++, target_addr += PAGE_SIZE,
         va_from += PAGE_SIZE) {
    if (unlikely(va_from >= vmr->bounds.space_end)) {
      if (ttree_cursor_next(&cursor) < 0) {
        ret = -ENOMEM;
        goto unlock_current;
      }

      vmr = ttree_item_from_cursor(&cursor);
      if (va_from < vmr->bounds.space_start) {
        ret = -ENOMEM;
        goto unlock_current;
      }
      if (vmr->flags & NON_GRANTABLE_VMRS_MASK) {
        ret = -EACCES;
        goto unlock_current;
      }
    }

    RPD_LOCK_WRITE(&current->task_mm->rpd);

    /*
     * All pages current task is granting to somebody must be present.
     * We're not going fault on each of them.
     */
    pidx = vaddr_to_pidx(&current->task_mm->rpd, va_from);
    if (pidx == PAGE_IDX_INVAL) {
      ret = -EFAULT;
      RPD_UNLOCK_WRITE(&current->task_mm->rpd);
      goto unlock_current;
    }

    page = pframe_by_id(pidx);
    /* Delete page from source's VM range. Source process */
    ret = memobj_method_call(vmr->memobj, delete_page, vmr, page);
    RPD_UNLOCK_WRITE(&current->task_mm->rpd);
    if (ret) {
      VMM_VERBOSE("[%s] Failed to delete page %#x from address %p\n",
                  vmm_get_name_dbg(current->task_mm),
                  pframe_number(page), va_from);
      
      goto unlock_current;
    }


    /*
     * Woops, we just met a page marked as copy-on-write.
     * No, it's not a tradegy, but it requires some extra work.
     * In simplest case - if target VM range has write access,
     * page content will be copied to fresh one, otherwise page
     * will be mapped as is.
     */
    if (unlikely((page->flags & PF_COW) && (target_vmr->flags & VMR_WRITE))) {
      page_frame_t *new_page = alloc_page(MMPOOL_USER | AF_ZERO);

      if (!new_page) {
        ret = -ENOMEM;
        goto unlock_current;
      }

      copy_page_frame(new_page, page);
      pin_page_frame(new_page);
      unpin_page_frame(page);
      page = new_page;
    }

    RPD_LOCK_WRITE(&target->task_mm->rpd);
    pidx = vaddr_to_pidx(&target->task_mm->rpd, target_addr);

    /*
     * If some page is already mapped to given address (it doesn't
     * matter which one exactly) we won't replace it by the page we've
     * just removed from the caller's address range.
     */
    if (pidx != PAGE_IDX_INVAL) {
      RPD_UNLOCK_WRITE(&target->task_mm->rpd);
      unpin_page_frame(page);
      ret = -EBUSY;
      goto unlock_current;
    }

    /* finally page may be inserted in target VM range */
    ret = memobj_method_call(target_vmr->memobj, insert_page, target_vmr,
                             page, target_addr, target_vmr->flags);
    RPD_UNLOCK_WRITE(&target->task_mm->rpd);
    if (ret) {      
      unpin_page_frame(page);
      goto unlock_current;
    }
  }

unlock_current:
  rwsem_up_read(&current->task_mm->rwsem);
unlock_target:
  rwsem_up_read(&target->task_mm->rwsem);

  release_task_struct(target);
  return ret;
}

#ifdef CONFIG_DEBUG_MM
#include <mstring/task.h>

void vmm_set_name_from_pid_dbg(vmm_t *vmm)
{
  memset(vmm->name_dbg, 0, VMM_DBG_NAME_LEN);
  snprintf(vmm->name_dbg, VMM_DBG_NAME_LEN, "VMM (PID: %ld)", vmm->owner->pid);
}

void vmm_enable_verbose_dbg(void)
{
  spinlock_lock(&__vmm_verb_lock);
  __vmm_verbose = true;
  spinlock_unlock(&__vmm_verb_lock);
}

void vmm_disable_verbose_dbg(void)
{
  spinlock_lock(&__vmm_verb_lock);
  __vmm_verbose = false;
  spinlock_unlock(&__vmm_verb_lock);
}

static void __print_tnode(ttree_node_t *tnode)
{
  int i;
  vmrange_t *vmr;

  tnode_for_each_index(tnode, i) {
    vmr = container_of(tnode_key(tnode, i), vmrange_t, bounds);
    kprintf("[%p<->%p FP: %zd), ", vmr->bounds.space_start,
            vmr->bounds.space_end, (vmr->hole_size >> PAGE_WIDTH));
  }

  kprintf("\n");
}

void vmranges_print_tree_dbg(vmm_t *vmm)
{
  ttree_print(&vmm->vmranges_tree, __print_tnode);
}

#endif /* CONFIG_DEBUG_MM */
