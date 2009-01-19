#include <config.h>
#include <ds/list.h>
#include <ds/ttree.h>
#include <mm/page.h>
#include <mm/pfalloc.h>
#include <mm/mmpools.h>
#include <mm/slab.h>
#include <mm/memobj.h>
#include <mm/vmm.h>
#include <mlibc/kprintf.h>
#include <mlibc/types.h>
#include <eza/arch/mm.h>

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

void vmm_initialize(void)
{
  mm_pool_t *pool;
  page_frame_iterator_t pfi;
  ITERATOR_CTX(page_frame, PF_ITER_ARCH) pfi_arch_ctx;

  arch_mm_init();
  mmpools_init();

  /*
   * PF_ITER_ARCH page frame iterator iterates through page_frame_t 
   * structures located in the page_frames_array. It starts from the
   * very first page and iterates forward until the last page available
   * in the system is handled. On each iteration it returns an
   * index of initialized by arhitecture-dependent level page frame.
   */
  pfi_arch_init(&pfi, &pfi_arch_ctx);
  
  /* initialize page and add it to the related pool */
  iterate_forward(&pfi) {
    page_frame_t *page = pframe_by_number(pfi.pf_idx);
    __init_page(page);
    mmpools_add_page(page);
  }

  kprintf("[MM] Memory pools were initialized\n");
  
  /*
   * Now we may initialize "init data allocator"
   * Note: idalloc allocator will cut from general pool's
   * pages no more than CONFIG_IDALLOC_PAGES. After initialization
   * is done, idalloc must be explicitely disabled.
   */
  pool = mmpools_get_pool(POOL_GENERAL);
  ASSERT(pool->free_pages);
  idalloc_enable(pool);
  kprintf("[MM] Init-data memory allocator was initialized.\n");
  kprintf(" idalloc available pages: %ld\n", idalloc_meminfo.npages);  
  for_each_active_mm_pool(pool) {
    char *name = mmpools_get_pool_name(pool->type);
    
    kprintf("[MM] Pages statistics of pool \"%s\":\n", name);
    kprintf(" | %-8s %-8s %-8s |\n", "Total", "Free", "Reserved");
    kprintf(" | %-8d %-8d %-8d |\n", pool->total_pages,
            atomic_get(&pool->free_pages), pool->reserved_pages);
    mmpools_init_pool_allocator(pool);
  }
  if (ptable_rpd_initialize(&kernel_rpd))
    panic("mm_init: Can't initialize kernel root page directory!");
  
  /* Now we can remap available memory */
  arch_mm_remap_pages();

  /* After all memory has been remapped, we can reserve some space
   * for initial virtual memory range allocation.
   */
  idalloc_meminfo.num_vpages=IDALLOC_VPAGES;
  idalloc_meminfo.avail_vpages=IDALLOC_VPAGES;
  idalloc_meminfo.virt_top=kernel_min_vaddr;
  kernel_min_vaddr-=IDALLOC_VPAGES*PAGE_SIZE;

  memobj_subsystem_initialize();
  kprintf("[MM] All pages were successfully remapped.\n");
  __initialize_mandatory_areas();
  kprintf("[MM] All mandatory user areas were successfully created.\n");
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
