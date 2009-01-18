#include <config.h>
#include <ds/list.h>
#include <ds/ttree.h>
#include <mm/page.h>
#include <mm/pfalloc.h>
#include <mm/slab.h>
#include <mm/memobj.h>
#include <mm/vmm.h>
#include <mlibc/kprintf.h>
#include <mlibc/types.h>

static LIST_DEFINE(mandmaps_lst);
static int __mandpas_total = 0;
static memcache_t __vmms_cache = NULL;

static int __vmranges_cmp(void *r1, void *r2)
{
  struct range_bounds *range1, *range2;
  long diff;

  range1 = (struct range_bounds *)r1;
  range2 = (struct range_bounds *)r2;
  if ((diff = range1.space_start - range2.space_end) < 0) {
    if ((diff = range2.space_start - range1.space_end) > 0)
      return -1;

    return 0;
  }
  else
    return !!diff;
}

void vmm_subsystem_initialize(void)
{
  kprintf("[MM] Initializing VMM subsystem...");
  __vmms_cache = memcache_create("VMM objects cache", sizeof(vmm_t),
                                 DEFAULT_SLAB_PAGES, SMCF_PGEN | SMCF_GENERIC);
  if (!__vmms_cache)
    panic("vmm_subsystem_initialize: Can not create memory cache for VMM objects. ENOMEM");
}

void register_mandmap(vm_mandmap_t *mandmap, uintptr_t va_from, uintptr_t va_to, vmrange_flags_t vm_flags)
{
  memset(mandmap, 0, sizeof(mandmap));
  mandmap->bounds.space_start = PAGE_ALIGN_DOWN(va_from);
  mandmap->bounds.space_end = PAGE_ALIGN(va_to);
  mandmap->vmr_flags = flags;
  list_add2tail(&mandmaps_lst, &mandmap->node);
  __mandmaps_total++;
}

void unregister_manmap(vm_mandmap_t *mandmap)
{
  list_del(&mandmap->node);
  __mandmaps_total--;
}

vmm_t *vmm_create(void)
{
  vmm_t *vmm;

  vmm = alloc_from_memcache(&__vmms_cache);
  if (!vmm)
    return NULL;

  memset(vmm, 0, sizeof(*vmm));
  ttree_init(&vmm->vmranges, __vmranges_cmpf, vmrange_t, bounds);
  atomic_set(&vmm->vmm_users, 1);
  return vmm;
}

uintptr_t vmrange_map(memobj_t *memobj, void *addr, int npages, vmrange_flags_t flags, off_t offset)
{
  vmm_t *vmm = current_task()->vmm;
  vmrange_t *vmrange_prev;
  uintptr_t addr;
  int err = -EINVAL;
  tnode_meta_t vmr_tnode_meta;  

  if (addr) {
    struct range_bounds bounds;
    
    bounds.space_start = (uintptr_t)addr;
    bounds.space_end = bounds.space_start + (1UL << npages);
    vmrange_prev = ttree_lookup(&vmm->ttree, &bounds, &vmr_tnode_meta);
  }

  error:
  return err;
}
