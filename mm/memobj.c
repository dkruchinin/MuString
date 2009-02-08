#include <config.h>
#include <ds/ttree.h>
#include <ds/list.h>
#include <ds/idx_allocator.h>
#include <mm/page.h>
#include <mm/pfalloc.h>
#include <mm/slab.h>
#include <mm/memobj.h>
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
    page_frame_t *pf = alloc_page(AF_PGEN | AF_ZERO | AF_CLEAR_RC);

    if (!pf)
      return -ENOMEM;

    ret = mmap_core(&vmr->parent_vmm->rpd, addr,
                    pframe_number(pf), 1, vmr->flags & VMR_PROTO_MASK);
    if (ret)
      free_page(pf);
  }
  else {
    page_idx_t idx = mm_vaddr2page_idx(&vmm->rpd, addr);

    ASSERT(idx != PAGE_IDX_INVAL);
    ret = mmap_core(&vmm->rpd, addr, idx, 1, vmr->flags & VMR_PROTO_MASK);
  }

  return ret;
}

static void __init_memobj(memobj_t *memobj, memobj_nature_t nature, off_t size)
{
  memobj->size = size;
  __ttree_init(&memobj->pagemap, __memobjs_cmp_func, 0);
  mutex_initialize(&memobj->mutex);
  if (nature & MEMOBJ_GENERIC) {
    memobj->mops.handle_page_fault = generic_handle_page_fault;
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
