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
#include <ds/iterator.h>
#include <mm/page.h>
#include <mm/pfalloc.h>
#include <mm/slab.h>
#include <mm/memobj.h>
#include <mm/vmm.h>
#include <mm/pfi.h>
#include <mm/mman.h>
#include <eza/mutex.h>
#include <eza/rwsem.h>
#include <eza/spinlock.h>
#include <eza/security.h>
#include <eza/usercopy.h>
#include <mlibc/kprintf.h>
#include <mlibc/types.h>

rpd_t kernel_rpd; /* kernel root page directory */
static memcache_t *__vmms_cache = NULL; /* Memory cache for vmm_t structures */
static memcache_t *__vmrs_cache = NULL; /* Memory cache for vmrange_t structures */
static LIST_DEFINE(__mandmaps_lst); /* Mandatory mappings list */
static int __num_mandmaps = 0; /* Total number of mandatory mappings */

#ifdef CONFIG_DEBUG_MM
static bool __vmm_verbose = false;
static SPINLOCK_DEFINE(__vmm_verb_lock);

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
           || (vmr->flags & VMR_NONE));
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

#define __VMR_CLEAR_MASK (VMR_FIXED | VMR_POPULATE)/* VMR_FIXED and VMR_POPULATE don't affect vmranges merging */

/*
 * returns true if VM range "vmr" can be merged with another VM range
 * having memory object "memobj" and flags = "flags"
 */
static inline bool can_be_merged(const vmrange_t *vmr, const memobj_t *memobj, vmrange_flags_t flags)
{
    return (((vmr->flags & ~__VMR_CLEAR_MASK) == (flags & ~__VMR_CLEAR_MASK))
          && ((vmr->memobj == memobj)));
}

/*
 * Create new VM range owned by vmm_t "parent_vmm".
 * New range will be attached to memory object "memobj". Its space_start will be equal to "va_start",
 * and space_end will be equal to va_start + (npages << PAGE_WIDTH).
 */
static vmrange_t *create_vmrange(vmm_t *parent_vmm, memobj_t *memobj,  uintptr_t va_start,
                                 page_idx_t npages, pgoff_t offset, vmrange_flags_t flags)
{
  vmrange_t *vmr;

  vmr = alloc_from_memcache(__vmrs_cache);
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
  ASSERT(prev_vmr->parent_vmm == next_vmr->parent_vmm);
  ASSERT(can_be_merged(prev_vmr, next_vmr->memobj, next_vmr->flags));
  ASSERT(prev_vmr->bounds.space_end == next_vmr->bounds.space_start);
  ASSERT(addr2pgoff(prev_vmr, prev_vmr->bounds.space_end) == next_vmr->offset);

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
 * Split VM range "vmrange" to two unique ranges by diapason [va_from, va_to).
 * Return newly created VM range started from va_to and ended with vmrange->bounds.space_end.
 */
static vmrange_t *split_vmrange(vmrange_t *vmrange, uintptr_t va_from, uintptr_t va_to)
{
  vmrange_t *new_vmr;
  page_idx_t npages;

  ASSERT(!(va_from & PAGE_MASK));
  ASSERT(!(va_to & PAGE_MASK));
  ASSERT((va_from > vmrange->bounds.space_start) && (va_to < vmrange->bounds.space_end));

  /*
   * vmrange will be splitted to two unique ranges: both will have the same
   * memory objects and flags. The first one(new) will lay from vmrange->bounds.space_start
   * to va_from exclusively and the second one will lay from va_to to vmrange->bounds.space_end
   * exclusively.
   */
  npages = (vmrange->bounds.space_end - va_to) >> PAGE_WIDTH;
  new_vmr = create_vmrange(vmrange->parent_vmm, vmrange->memobj, va_to, npages,
                           addr2pgoff(vmrange, va_to), vmrange->flags);
  if (!new_vmr)
    return NULL;

  VMM_VERBOSE("%s: Split VM range [%p, %p) to [%p, %p) and [%p, %p)\n",
              vmm_get_name_dbg(vmrange->parent_vmm), vmrange->bounds.space_start,
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
static void fix_vmrange_holes_after_insertion(vmm_t *vmm, vmrange_t *vmrange, ttree_cursor_t *cursor)
{
  vmrange_t *vmr;
  ttree_cursor_t csr;

  ttree_cursor_copy(&csr, cursor);
  if (!ttree_cursor_prev(&csr)) {
    vmr = ttree_item_from_cursor(&csr);
    VMM_VERBOSE("%s(P) [%p, %p): old hole size: %ld, new hole size: %ld\n",
                vmm_get_name_dbg(vmm), vmr->bounds.space_start, vmr->bounds.space_end,
                vmr->hole_size, vmrange->bounds.space_start - vmr->bounds.space_end);
    vmr->hole_size = vmrange->bounds.space_start - vmr->bounds.space_end;
  }

  ttree_cursor_copy(&csr, cursor);
  if (!ttree_cursor_next(&csr)) {
    vmr = ttree_item_from_cursor(&csr);
    VMM_VERBOSE("%s(N) [%p, %p): old hole size: %ld, new hole size: %ld\n",
                vmm_get_name_dbg(vmm), vmrange->bounds.space_start, vmrange->bounds.space_end,
                vmrange->hole_size, vmr->bounds.space_start - vmrange->bounds.space_end);    
    vmrange->hole_size = vmr->bounds.space_start - vmrange->bounds.space_end;
  }
  else {
    /*
     * If next VM range doensn't exist, calculate hole size from user-space
     * top virtual address.
     */

    VMM_VERBOSE("%s(L) [%p, %p): old hole size: %ld, new hole size: %ld\n",
                vmm_get_name_dbg(vmm), vmrange->bounds.space_start, vmrange->bounds.space_end,
                vmrange->hole_size, USPACE_VA_TOP - vmrange->bounds.space_end);
    vmrange->hole_size = USPACE_VA_TOP - vmrange->bounds.space_end;
  }
}

void vm_mandmap_register(vm_mandmap_t *mandmap, const char *mandmap_name)
{
#ifndef CONFIG_TEST
  ASSERT(!valid_user_address_range(mandmap->virt_addr, mandmap->num_pages << PAGE_WIDTH));
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

  list_for_each_entry(&__mandmaps_lst, mandmap, node) {
    status = mmap_core(&target_mm->rpd, mandmap->virt_addr, mandmap->phys_addr >> PAGE_WIDTH,
                       mandmap->num_pages, mandmap->flags, true);
    VMM_VERBOSE("[%s]: Mandatory mapping \"%s\" [%p -> %p] was initialized\n",
                vmm_get_name_dbg(target_mm), mandmap->name,
                mandmap->virt_addr, mandmap->virt_addr + (mandmap->num_pages << PAGE_WIDTH));
    if (status) {
      kprintf(KO_ERROR "Mandatory mapping \"%s\" failed [err=%d]: Can't mmap region %p -> %p starting"
              " from page with index #%x!\n",
              mandmap->name, status, mandmap->virt_addr, mandmap->virt_addr + (mandmap->num_pages << PAGE_WIDTH),
              mandmap->phys_addr << PAGE_WIDTH);
      goto out;
    }
  }

  out:
  return status;
}

void vmm_subsystem_initialize(void)
{
  kprintf("[MM] Initializing VMM subsystem...\n");
  __vmms_cache = create_memcache("VMM objects cache", sizeof(vmm_t),
                                 DEFAULT_SLAB_PAGES, SMCF_PGEN | SMCF_GENERIC);
  if (!__vmms_cache)
    panic("vmm_subsystem_initialize: Can not create memory cache for VMM objects. ENOMEM");

  __vmrs_cache = create_memcache("Vmrange objects cache", sizeof(vmrange_t),
                                 DEFAULT_SLAB_PAGES, SMCF_PGEN | SMCF_GENERIC);
  if (!__vmrs_cache)
    panic("vmm_subsystem_initialize: Can not create memory cache for vmrange objects. ENOMEM.");
}

vmm_t *vmm_create(void)
{
  vmm_t *vmm;

  vmm = alloc_from_memcache(__vmms_cache);
  if (!vmm)
    return NULL;

  memset(vmm, 0, sizeof(*vmm));
  ttree_init(&vmm->vmranges_tree, __vmranges_cmp, vmrange_t, bounds);
  rwsem_initialize(&vmm->rwsem);
  if (ptable_ops.initialize_rpd(&vmm->rpd) < 0) {
    memfree(vmm);
    vmm = NULL;
  }

  return vmm;
}

/*
 * Clone all VM ranges in "src" to "dst" and map them into dst's root
 * page directory regarding the policy specified by the "flags".
 */
int vmm_clone(vmm_t *dst, vmm_t *src, int flags)
{
  ttree_node_t *tnode;
  int i = 0, ret = 0;
  vmrange_t *vmr, *new_vmr;
  uintptr_t addr;
  pde_t *pde;
  page_idx_t pidx;
  vmrange_flags_t new_vmr_flags;

  ASSERT(dst != src);
  ASSERT((flags & (VMM_CLONE_POPULATE | VMM_CLONE_COW)) != (VMM_CLONE_POPULATE | VMM_CLONE_COW));
  
  rwsem_down_write(&src->rwsem);
  pagetable_lock(&src->rpd);
  tnode = ttree_tnode_leftmost(src->vmranges_tree.root);
  ASSERT(tnode != NULL);

  /* Browse throug each VM range in src and copy its content into the dst's VM ranges tree */
  while (tnode) {
    tnode_for_each_index(tnode, i) {
      vmr = ttree_key2item(&src->vmranges_tree, tnode_key(tnode, i));
      new_vmr_flags = vmr->flags;

      /*
       * If current VM range is shared and VMM_CLONE shared wasn't specified, pages
       * in src's VM range would be copied to the dst's address space. To do this properly
       * we have to change VMR_SHARED to VMR_PRIVATE.
       */
      if ((vmr->flags & VMR_SHARED) && !(flags & VMM_CLONE_SHARED)) {
        new_vmr_flags &= ~VMR_SHARED;
        new_vmr_flags |= VMR_PRIVATE;
      }
      else if ((vmr->flags & VMR_PHYS) && !(flags & VMM_CLONE_PHYS))
        continue;

      new_vmr = create_vmrange(dst, vmr->memobj, vmr->bounds.space_start,
                               (vmr->bounds.space_end - vmr->bounds.space_start) >> PAGE_WIDTH,
                               vmr->offset, new_vmr_flags);
      if (!new_vmr) {
        ret = -ENOMEM;
        goto clone_failed;
      }

      new_vmr->hole_size = vmr->hole_size;
      ASSERT(!ttree_insert(&dst->vmranges_tree, new_vmr));

      /* Handle each mapped page in given range regarding the specified clone policy */
      for (addr = vmr->bounds.space_start; addr < vmr->bounds.space_end; addr += PAGE_SIZE) {
        pidx = ptable_ops.vaddr2page_idx(&src->rpd, addr, &pde);
        
        /*
         * In some situations page may not be present in src's page table because it was
         * swapped before vmm_clone is called. In this case, we make a copy of the lowest
         * level PDE into the dst's page table.
         */
        if (pidx == PAGE_IDX_INVAL) {
          if (pde != NULL) {
            pidx = pde_fetch_page_idx(pde);
            ret = mmap_core(&dst->rpd, addr, pidx, 1, pde_get_flags(pde), false);
            if (ret)
              goto clone_failed;
          }

          continue;
        }
        if (!(vmr->flags & VMR_SHARED) && (vmr->flags & VMR_WRITE) && (flags & VMM_CLONE_COW)) {
        /*
         * If copy-on-write clone policy was specified and current cloned VM range is writable and
         * not shared, we have to remap it with read-only access policy in both src and dst.
         * After page fault is occured, the page will be copied into another one and unpined.
         */
          page_frame_t *page = pframe_by_number(pidx);

          new_vmr_flags &= ~VMR_WRITE;
          page->flags |= PF_COW; /* PF_COW flag tells memory object about nature of the fault */

          /* Remap page to read-only in src */
          ret = mmap_core(&src->rpd, addr, pidx, 1,
                          ((vmr->flags & KMAP_FLAGS_MASK) & ~VMR_WRITE), false);
          if (ret)
            goto clone_failed;
        }
        else if ((flags & VMM_CLONE_POPULATE) &&
                 ((((vmr->flags & VMR_SHARED) && !(flags & VMM_CLONE_SHARED))) || (vmr->flags & VMR_WRITE))) {
          /*
           * There are two possible situations when we have to explicitely allocate new page and copy
           * the content of the current page into it in order to map it to the dst's page table later:
           *  1) VMM_CLONE_POPULATE clone policy was specified and 
           */
          page_frame_t *page = alloc_page(AF_USER | AF_ZERO);

          if (!page) {
            ret = -ENOMEM;
            goto clone_failed;
          }

          copy_page_frame(page, pframe_by_number(pidx));
          pidx = pframe_number(page);
        }

        ret = mmap_core(&dst->rpd, addr, pidx, 1, new_vmr_flags & KMAP_FLAGS_MASK, true);
        if (ret)
          goto clone_failed;
      }
    }

    tnode = tnode->successor;
  }

  pagetable_unlock(&src->rpd);
  rwsem_up_write(&src->rwsem);
  return ret;

  clone_failed:
  pagetable_unlock(&src->rpd);
  rwsem_up_write(&src->rwsem);
  //vmm_destroy(dst);

  return ret;
}

/* Find room for VM range with length "length". */
uintptr_t find_free_vmrange(vmm_t *vmm, uintptr_t length, ttree_cursor_t *cursor)
{
  uintptr_t start = USPACE_VA_BOTTOM;
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

vmrange_t *vmrange_find(vmm_t *vmm, uintptr_t va_start, uintptr_t va_end, ttree_cursor_t *cursor)
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
void vmranges_find_covered(vmm_t *vmm, uintptr_t va_from, uintptr_t va_to, vmrange_set_t *vmrs)
{
  ASSERT(!(va_from & PAGE_MASK) && !(va_to & PAGE_MASK));
  memset(vmrs, 0, sizeof(*vmrs));
  vmrs->va_from = va_from;
  vmrs->va_to = va_to;
  ttree_cursor_init(&vmm->vmranges_tree, &vmrs->cursor);
  vmrs->vmr = vmrange_find(vmm, va_from, va_from + PAGE_SIZE, &vmrs->cursor);
  if (!vmrs->vmr) {
    if (!ttree_cursor_next(&vmrs->cursor)) {
      vmrs->vmr = ttree_item_from_cursor(&vmrs->cursor);
      if (vmrs->vmr->bounds.space_end > va_to)
        vmrs->vmr = NULL;
    }
  }
}

long vmrange_map(memobj_t *memobj, vmm_t *vmm, uintptr_t addr, page_idx_t npages,
                 vmrange_flags_t flags, pgoff_t offset)
{
  vmrange_t *vmr;
  ttree_cursor_t cursor, csr_tmp;
  int err = 0;
  bool was_merged = false;

  vmr = NULL;
  ASSERT(memobj != NULL);
  ttree_cursor_init(&vmm->vmranges_tree, &cursor);

  /* general checking */
  if (!(flags & VMR_PROTO_MASK) /* protocol must be set */
      || !(flags & (VMR_PRIVATE | VMR_SHARED)) /* private *or* shared flags must be set */
      || ((flags & (VMR_PRIVATE | VMR_SHARED)) == (VMR_PRIVATE | VMR_SHARED)) /* but they mustn't be set together */
      || !npages /* npages must not be zero */
      || ((flags & VMR_NONE) && ((flags & VMR_PROTO_MASK) != VMR_NONE)) /* VMR_NONE must not be set with VMR_SHARED or VMR_PRIVATE */
      || (flags & (VMR_PHYS | VMR_SHARED)) == (VMR_PHYS | VMR_SHARED)   /* VMR_SHARED can not coexist with VMR_PHYS */
      || ((flags & VMR_FIXED) && !addr)) /* VMR_FIXED expects that address is specified */
  {
    err = -EINVAL;
    goto err;
  }

  /* If corresponding memory object doesn't support shared memory facility, return an error. */
  if ((flags & VMR_SHARED) && (memobj->flags & MMO_FLG_NOSHARED)) {
    kprintf("WTF? id = %d\n", memobj->id);
    err = -ENOTSUP;
    goto err;
  }
  if (flags & VMR_PHYS) {
    flags |= VMR_NOCACHE; /* physical pages must not be cached */
    if (!trusted_task(current_task())) { /* and task taht maps them must be trusted */
      err = -EPERM;
      goto err;
    }

    /* only generic memory object supports physical mappings */
    if (memobj->id != GENERIC_MEMOBJ_ID) {
      err = -ENOTSUP;
      goto err;
    }
  }
  if (addr) {
    if (!valid_user_address_range(addr, (npages << PAGE_WIDTH))) {
      if (flags & VMR_FIXED) {
        err = -ENOMEM;
        goto err;
      }

      goto allocate_address;
    }

    vmr = vmrange_find(vmm, addr, addr + (npages << PAGE_WIDTH), &cursor);
    if (vmr) {
      if (flags & VMR_FIXED) {
        VMM_VERBOSE("%s: Failed to allocate fixed VM range [%p, %p) because it is "
                    "covered by this one: [%p, %p)\n", vmm_get_name_dbg(vmm), addr,
                    addr + (npages << PAGE_WIDTH), vmr->bounds.space_start, vmr->bounds.space_end);
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
   * physical mappings. Unlike other memory objects it actually doesn't have *real*(I mean
   * tied with anything) offset, but it has to have it in order to be semantically compatible with
   * other types of memory objects. If the mapping is anonymous, offset will be equal to page index
   * of the bottom virtual address of VM range. Otherwise(i.e. if mapping is physical), offset is already
   * specified by the user.
   */
  if ((memobj->id == GENERIC_MEMOBJ_ID) && !(flags & VMR_PHYS))
    offset = addr >> PAGE_WIDTH;
  if ((offset + npages) >= memobj->size) {
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
      vmr->hole_size -= (addr + (npages << PAGE_WIDTH) - vmr->bounds.space_end);
      vmr->bounds.space_end = addr + (npages << PAGE_WIDTH);
      was_merged = true;
    }
  }

  ttree_cursor_copy(&csr_tmp, &cursor);
  if (!ttree_cursor_next(&csr_tmp)) {
    vmrange_t *prev = vmr;

    vmr = ttree_item_from_cursor(&csr_tmp);

    /* Try attach new mapping to the bottom of the next VM range. */
    if (can_be_merged(vmr, memobj, flags)) {
      if (!was_merged && (vmr->bounds.space_start == (addr + (npages << PAGE_WIDTH))) &&
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
      else if (was_merged && (prev->bounds.space_end == vmr->bounds.space_start) &&
               (addr2pgoff(prev, prev->bounds.space_end) == vmr->offset)) {
        /*
         * If the mapping was merged with either previous or next VM ranges,
         * and after it they become mergeable, merge them.
         */
        vmr = merge_vmranges(prev, vmr);
      }
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
    if (err) {
      goto err;
    }
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
  uintptr_t va_to = va_from + (npages << PAGE_WIDTH);
  vmrange_t *vmr, *vmr_prev = NULL;

  ASSERT(!(va_from & PAGE_MASK));
  ttree_cursor_init(&vmm->vmranges_tree, &cursor);

  /*
   * At first try to find a diapason containing range [va_from, va_from + PAGE_SIZE),
   * i.e. minimum starting range. If it wasn't found, try get the next one in the tree
   * using T*-tree cursor. Due its nature it will return the next item in the tree(if exists)
   * after position where [va_from, va_from + PAGE_SIZE) could be. If such item is found and
   * if it has appropriate bounds, it is the very first VM range covered by given diapason.
   */
  vmr = vmrange_find(vmm, va_from, va_to, &cursor);
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
    if (!split_vmrange(vmr, va_from, va_to)) {
      kprintf(KO_ERROR "unmap_vmranges: Failed to split VM range [%p, %p). -ENOMEM\n",
              vmr->bounds.space_start, vmr->bounds.space_end);
      return -ENOMEM;
    }

    pagetable_lock(&vmm->rpd);
    munmap_core(&vmm->rpd, va_from, npages, true);
    pagetable_unlock(&vmm->rpd);

    return 0; /* done */
  }
  else if (va_from > vmr->bounds.space_start) {
    uintptr_t new_end = va_from;

    /*
     * va_from may be greater than the bottom virtual address of
     * given range. If so, range's top address must be decreased 
     * regarding the new top address.
     */
    pagetable_lock(&vmm->rpd);
    munmap_core(&vmm->rpd, va_from,
                (vmr->bounds.space_end - va_from) >> PAGE_WIDTH, true);
    pagetable_unlock(&vmm->rpd);
    va_from = vmr->bounds.space_end;
    vmr->bounds.space_end = new_end;
    vmr_prev = vmr; /* Remember this VM range as a previous. Later we'll fix its hole size. */
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
      munmap_core(&vmm->rpd, va_from, ((va_to - va_from) >> PAGE_WIDTH), true);
      break;
    }

    pagetable_lock(&vmm->rpd);
    munmap_core(&vmm->rpd, va_from, (vmr->bounds.space_end - va_from) >> PAGE_WIDTH, true);
    pagetable_unlock(&vmm->rpd);
    ttree_delete_placeful(&cursor);
    destroy_vmrange(vmr);
    vmr = NULL;
    if (cursor.state != TT_CSR_TIED)
      break;

    /*
     * Due the nature of T*-tree, after deletion by the cursor is done,
     * the cursor points to the next item in a tree(if exists).
     * So we can safely fetch an item from the cursor(cursor validation was done above).
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
    if (unlikely(vmr == NULL)) /* vmr_prev is highest VM range in an address space */
      vmr_prev->hole_size = USPACE_VA_TOP - vmr_prev->bounds.space_end;
    else
      vmr_prev->hole_size = vmr->bounds.space_start - vmr_prev->bounds.space_end;
  }

  return 0;
}

int mmap_core(rpd_t *rpd, uintptr_t va, page_idx_t first_page,
              page_idx_t npages, kmap_flags_t flags, bool pin_pages)
{
  page_frame_iterator_t pfi;
  ITERATOR_CTX(page_frame, PF_ITER_INDEX) pf_idx_ctx;

  if ((va & PAGE_MASK) || !npages)
    return -EINVAL;

  pfi_index_init(&pfi, &pf_idx_ctx, first_page, first_page + npages - 1);
  iter_first(&pfi);
  return __mmap_core(rpd, va, npages, &pfi, flags, pin_pages);
}

int fault_in_user_pages(vmm_t *vmm, uintptr_t address, size_t length, uint32_t pfmask,
                        void (*callback)(vmrange_t *vmr, page_frame_t *page, void *data), void *data)
{
  page_idx_t pidx;
  int ret = 0;
  pde_t *pde;
  vmrange_flags_t vmr_mask = VMR_READ;
  uintptr_t va;
  pgoff_t npages, i = 0;
  vmrange_t *vmr;
  ttree_cursor_t cursor;

  va = PAGE_ALIGN(address + length);
  npages = (va - PAGE_ALIGN_DOWN(address)) >> PAGE_WIDTH;
  va = PAGE_ALIGN_DOWN(address);
  
  if (!valid_user_address_range(va, va + (npages << PAGE_WIDTH)))
    return -EFAULT;
  if (pfmask & PFLT_WRITE)
    vmr_mask |= VMR_WRITE;

  vmr = vmrange_find(vmm, va, address, &cursor);
  if (!vmr)
    return -EFAULT;
  if (!__valid_vmr_rights(vmr, pfmask))
    return -EACCES;
  
  while (i < npages) {
    if (unlikely(va >= vmr->bounds.space_end)) {
      if (ttree_cursor_next(&cursor) < 0)
        return -EFAULT;

      vmr = ttree_item_from_cursor(&cursor);
      if (va < vmr->bounds.space_start)
        return -EFAULT;
      if (!__valid_vmr_rights(vmr, pfmask))
        return -EACCES;
    }
    
    pagetable_lock(&vmm->rpd);
    pidx = ptable_ops.vaddr2page_idx(&vmm->rpd, va, &pde);
    if (pidx != PAGE_IDX_INVAL) {
      if ((ptable_to_kmap_flags(pde_get_flags(pde)) & vmr_mask) == vmr_mask) {
        pagetable_unlock(&vmm->rpd);
        goto eof_fault;
      }      
    }
    else
      pfmask |= PFLT_NOT_PRESENT;
    
    pagetable_unlock(&vmm->rpd);
    ret = memobj_method_call(vmr->memobj, handle_page_fault, vmr, va, pfmask);
    if (ret)
      return ret;

    pidx = ptable_ops.vaddr2page_idx(&vmm->rpd, va, &pde);
    ASSERT(pidx != PAGE_IDX_INVAL);
    
    eof_fault:
    if (callback)
      callback(vmr, pframe_by_number(pidx), data);

    va += PAGE_SIZE;
    i++;
    pfmask &= ~PFLT_NOT_PRESENT;
  }

  return 0;
}

int vmm_handle_page_fault(vmm_t *vmm, uintptr_t fault_addr, uint32_t pfmask)
{
  int ret;
  vmrange_t *vmr;
  memobj_t *memobj;

  rwsem_down_read(&vmm->rwsem);
  vmr = vmrange_find(vmm, PAGE_ALIGN_DOWN(fault_addr), fault_addr, NULL);
  if (!vmr) {
    return -EFAULT;
  }

  /*
   * If fault was caused by write attemption and VM range that was found
   * has VMR_WRITE flag unset or VMR_NONE flag set, fault can not be handled.
   */
  if (!__valid_vmr_rights(vmr, pfmask))
    return -EACCES;

  ASSERT(vmr->memobj != NULL);
  memobj = vmr->memobj;
  ret = memobj_method_call(memobj, handle_page_fault, vmr, PAGE_ALIGN_DOWN(fault_addr), pfmask);
  rwsem_up_read(&vmm->rwsem);
  
  return ret;
}

long sys_mmap(pid_t victim, memobj_id_t memobj_id, struct mmap_args *uargs)
{
  task_t *victim_task;
  memobj_t *memobj = NULL;
  vmm_t *vmm;
  long ret;
  struct mmap_args margs;
  vmrange_flags_t vmrflags;

  if (likely(!victim))
    victim_task = current_task();
  else {
    victim_task = pid_to_task(victim);
    if (!victim_task)
      return -ESRCH;
  }

  vmm = victim_task->task_mm;
  if (copy_from_user(&margs, uargs, sizeof(margs)))
    return -EFAULT;

  vmrflags = (margs.prot & VMR_PROTO_MASK) | (margs.flags << VMR_FLAGS_OFFS);
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
  if (memobj)
    unpin_memobj(memobj);

  return ret;
}

int sys_munmap(pid_t victim, uintptr_t addr, size_t length)
{
  task_t *victim_task;
  vmm_t *vmm;
  int ret;

  if (likely(!victim))
    victim_task = current_task();
  else {
    victim_task = pid_to_task(victim);
    if (!victim_task)
      return -ESRCH;
  }

  vmm = victim_task->task_mm;
  
  /* address must be page aligned and length must be specified */
  if (!length || (addr & PAGE_MASK))
    return -EINVAL;

  length = PAGE_ALIGN(length);
  if (!valid_user_address_range(addr, length))
    return -EINVAL;

  rwsem_down_write(&vmm->rwsem);
  ret = unmap_vmranges(vmm, addr, length >> PAGE_WIDTH);
  rwsem_up_write(&vmm->rwsem);

  return ret;
}

#ifdef CONFIG_DEBUG_MM
#include <eza/task.h>

void vmm_set_name_from_pid_dbg(vmm_t *vmm, unsigned long pid)
{
  if (is_tid(pid))
    pid = TID_TO_PIDBASE(pid);

  memset(vmm->name_dbg, 0, VMM_DBG_NAME_LEN);
  snprintf(vmm->name_dbg, VMM_DBG_NAME_LEN, "VMM [PID: %ld]", pid);
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
