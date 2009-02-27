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
 * (c) Copyright 2009 Dan Kruchinin <dan.kruchinin@gmail.com>
 *
 * mm/memobj.c - Memory objects subsystem
 *
 */

#include <config.h>
#include <ds/ttree.h>
#include <ds/list.h>
#include <ds/idx_allocator.h>
#include <mm/page.h>
#include <mm/pfalloc.h>
#include <mm/slab.h>
#include <mm/memobj.h>
#include <mm/pfi.h>
#include <mlibc/kprintf.h>
#include <mlibc/types.h>
#include <eza/arch/mm.h>

static idx_allocator_t __memobjs_ida;
static memcache_t *__memobjs_memcache = NULL;
static ttree_t __memobjs_tree;
memobj_t null_memobj;

static int __memobjs_cmp_func(void *k1, void *k2)
{
  return (*(long *)k1 - *(long *)k2);
}

static int generic_handle_page_fault(memobj_t *memobj, vmrange_t *vmr, off_t off, uint32_t pfmask)
{
  uintptr_t addr = vmr->bounds.space_start + off;
  int ret = 0;
  vmm_t *vmm = vmr->parent_vmm;

  ASSERT(vmm != NULL);
  ASSERT(vmr->memobj == memobj);
  if (pfmask & PFLT_NOT_PRESENT) {
    page_frame_t *pf = alloc_page(AF_USER | AF_ZERO);

    if (!pf)
      return -ENOMEM;

    ret = mmap_core(&vmr->parent_vmm->rpd, addr,
                    pframe_number(pf), 1, vmr->flags & VMR_PROTO_MASK);
    if (ret)
      free_page(pf);
  }
  else {
    page_idx_t idx = ptable_ops.vaddr2page_idx(&vmm->rpd, addr);

    ASSERT(idx != PAGE_IDX_INVAL);
    ret = mmap_core(&vmm->rpd, addr, idx, 1, vmr->flags & VMR_PROTO_MASK);
  }

  return ret;
}

static int generic_populate_pages(memobj_t *memobj, vmrange_t *vmr, uintptr_t addr,
                                  page_idx_t npages, off_t offs_pages)
{
  int ret;
  page_frame_t *pages;
  page_frame_iterator_t pfi;
  ITERATOR_CTX(page_frame, PF_ITER_LIST) list_ctx;
  
  ASSERT(memobj->id == NULL_MEMOBJ_ID);
  ASSERT(vmr->parent_vmm != NULL);
  pages = alloc_pages(npages, AF_ZERO | AF_USER);
  if (!pages)
    return -ENOMEM;

  pfi_list_init(&pfi, &list_ctx, &pages->chain_node, pages->chain_node.prev);
  iter_first(&pfi);
  ret = __mmap_core(&vmr->parent_vmm->rpd, addr, npages, &pfi, vmr->flags & KMAP_FLAGS_MASK);
  if (ret)
    free_pages_chain(pages);

  return ret;
}

static void __init_memobj(memobj_t *memobj, memobj_nature_t nature, off_t size)
{
  memobj->size = size;
  __ttree_init(&memobj->pagemap, __memobjs_cmp_func, 0);
  mutex_initialize(&memobj->mutex);
  if (nature & MEMOBJ_GENERIC) {
    memobj->mops.handle_page_fault = generic_handle_page_fault;
    memobj->mops.populate_pages = generic_populate_pages;
    memobj->nature = MEMOBJ_GENERIC;
  }
}

void memobj_subsystem_initialize(void)
{
  kprintf("[MM] Initializing memory objects subsystem...\n");
  idx_allocator_init(&__memobjs_ida, CONFIG_MEMOBJS_MAX);
  idx_reserve(&__memobjs_ida, 0);
  __memobjs_memcache = create_memcache("Mmemory objects cache", sizeof(memobj_t),
                                       DEFAULT_SLAB_PAGES, SMCF_PGEN | SMCF_GENERIC);
  if (!__memobjs_memcache)
    panic("memobj_subsystem_initialize: Can't create memory cache for memory objects. ENOMEM.");

  ttree_init(&__memobjs_tree, __memobjs_cmp_func, memobj_t, id);
  __init_memobj(&null_memobj, MEMOBJ_GENERIC, 0);
}

memobj_t *memobj_create(memobj_nature_t nature, off_t size)
{
  memobj_t *memobj = alloc_from_memcache(__memobjs_memcache);

  if (!memobj)
    return NULL;

  __init_memobj(memobj, nature, size);
  return memobj;
}

memobj_t *memobj_find_by_id(memobj_id_t memobj_id)
{
  if (memobj_id == 0)
    return &null_memobj;
  
  return ttree_lookup(&__memobjs_tree, &memobj_id, NULL);
}
