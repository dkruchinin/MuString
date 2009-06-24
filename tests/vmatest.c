#include <test.h>
#include <config.h>
#include <mm/page.h>
#include <mm/vmm.h>
#include <mm/memobj.h>
#include <mm/page_alloc.h>
#include <kernel/syscalls.h>
#include <mstring/types.h>

#define VMA_TEST_ID "VMA test"

static bool is_completed = false;
static int __pr = 0;

static inline int __simple_random(void)
{
  __pr =  (11 * __pr + 0xbeaf) % 333;
  return __pr;
}

static inline void __validate_hole_sz(test_framework_t *tf, vmrange_t *vmr, uintptr_t exp_sz)
{
  if (vmr->hole_size & PAGE_MASK) {
      tf->printf("VM range [%p, %p): Hole size(%ld) is not page aligned!\n",
                 vmr->bounds.space_start, vmr->bounds.space_end, vmr->hole_size);
      tf->failed();
    }
    if (exp_sz != vmr->hole_size) {
      tf->printf("VM range [%p, %p). Hole size is %ld, but %ld is expected!\n",
                 vmr->bounds.space_start, vmr->bounds.space_end, vmr->hole_size, exp_sz);
      tf->failed();
    }
}

static void map_merge_test(test_framework_t *tf, vmm_t *vmm)
{
  long ret = 0, free_space = USPACE_VA_TOP - USPACE_VA_BOTTOM;
  page_idx_t pgs = 128;
  vmrange_t *vmr, *vmr2;
  uintptr_t last_va;
  
  for (;;) {
    tf->printf("Creating VM range of %d pages... ", pgs);
    ret = vmrange_map(NULL, vmm, 0, pgs,
                      VMR_ANON | VMR_PRIVATE | VMR_POPULATE | VMR_READ | VMR_WRITE, 0);
    if (ret & PAGE_MASK) {
      break;
    }

    tf->printf("[OK] (addr=%p)\n", ret);
    if (vmm->num_vmrs != 1) {
      tf->printf("Number of VM ranges in a vmranges_tree must be equal to 1, "
                 "but it's %d indeed!\n", vmm->num_vmrs);
      tf->failed();
    }
    
    vmr = vmrange_find(vmm, USPACE_VA_BOTTOM, USPACE_VA_BOTTOM + PAGE_SIZE, NULL);
    if (!vmr) {
      tf->printf("Can not find VM range including diapason %p -> %p\n",
                 USPACE_VA_BOTTOM, USPACE_VA_BOTTOM + PAGE_SIZE);
      tf->failed();
    }

    free_space -= (pgs << PAGE_WIDTH);
    __validate_hole_sz(tf, vmr, free_space);
    if ((ret + (pgs << PAGE_WIDTH)) != vmr->bounds.space_end) {
      tf->printf("addr(%p) + %ld != %p\n", ret, pgs << PAGE_WIDTH, vmr->bounds.space_end );
      tf->failed();
    }

    last_va = vmr->bounds.space_end;
    pgs += pgs / 2;
  }
  if (ret != -ENOMEM) {
    tf->printf("[FAILED]. -ENOMEM was expected\n");
    tf->failed();
  }
  else
    tf->printf("[OK]. Got -ENOMEM as was expected\n");

  free_space /= 2;
  pgs = free_space >> PAGE_WIDTH;
  tf->printf("Creatng fixed VM range ending at the top uspace vrtual address\n");
  ret = vmrange_map(NULL, vmm, last_va + free_space, pgs,
                    VMR_ANON | VMR_PRIVATE | VMR_FIXED | VMR_READ | VMR_WRITE, 0);
  tf->printf("Creating: (%p, %p]\n", last_va + free_space, last_va + free_space + (pgs << PAGE_WIDTH));
  if (ret & PAGE_MASK) {
    tf->printf("Failed to create fixed VM range: (%p, %p]: ERR = %d\n",
               last_va + free_space, last_va + free_space + (pgs << PAGE_WIDTH), ret);
    tf->failed();
  }

  vmr2 = vmrange_find(vmm, USPACE_VA_TOP - PAGE_SIZE, USPACE_VA_TOP, NULL);
  if (!vmr2) {
    tf->printf("Can not find VM range including interval [%p, %p)!\n",
               USPACE_VA_TOP - PAGE_SIZE, USPACE_VA_TOP);
    tf->failed();
  }

  __validate_hole_sz(tf, vmr2, 0);
  __validate_hole_sz(tf, vmr, free_space);
  last_va = vmr2->bounds.space_start;
  free_space = vmr->hole_size;
  pgs = 128;
  tf->printf("Fill the hole up by %d pages at each step...\n", pgs);
  for (;;) {
    if ((free_space >> PAGE_WIDTH) < pgs)
      pgs = free_space >> PAGE_WIDTH;
    
    tf->printf("Creating VM range [%p, %p)... ", last_va - PAGE_SIZE * pgs, last_va);
    ret = vmrange_map(NULL, vmm, last_va - PAGE_SIZE * pgs, pgs,
                      VMR_ANON | VMR_PRIVATE | VMR_FIXED | VMR_READ | VMR_WRITE, 0);
    if (ret & PAGE_MASK) {
      tf->printf("[FAILED]. ERR = %d\n", ret);
      tf->failed();
    }
    
    last_va -= PAGE_SIZE * pgs;
    free_space -= PAGE_SIZE * pgs;
    tf->printf("FP: %ld\n", free_space >> PAGE_WIDTH);
    tf->printf("[OK]. (addr = %p)\n", ret);
    vmr = vmrange_find(vmm, last_va, last_va + PAGE_SIZE * pgs, NULL);
    if (!vmr) {
      tf->printf("Can not find VM range including diapason %p -> %p\n",
                 last_va, last_va + PAGE_SIZE * pgs);
      tf->failed();
    }

    __validate_hole_sz(tf, vmr, 0);
    if (vmm->num_vmrs == 1)
      break;
    if (vmm->num_vmrs != 2) {
      tf->printf("Unexpected number of VM ranges in VMM: %d\n", vmm->num_vmrs);
      tf->failed();
    }
    if (vmr->bounds.space_start != last_va) {
      tf->printf("Unexpected start of VM range: %p. (expected %p)!\n",
                 vmr->bounds.space_start, last_va);
      tf->failed();
    }
    if (vmr->bounds.space_end != USPACE_VA_TOP) {
      tf->printf("Unexpected end of VM range: %p (expected %p)!\n",
                 vmr->bounds.space_end, USPACE_VA_TOP);
      tf->failed();
    }

    pgs *= 2;
  }
  if (vmm->num_vmrs != 1) {
    tf->printf("Unexpected number of VM ranges in VMM: %d. (1 was expected)!\n", vmm->num_vmrs);
    tf->failed();
  }
  
  tf->printf("OK\n");
}

static void __unmap_chunk(test_framework_t *tf, vmm_t *vmm, uintptr_t va_from,
                          page_idx_t pages, ttree_cursor_t *csr)
{
  tf->printf("Unmap diapason %p, %p...\n", va_from, va_from + (pages << PAGE_WIDTH));
  unmap_vmranges(vmm, va_from, pages);
  if (!vmrange_find(vmm, va_from - PAGE_SIZE, va_from + (pages << PAGE_WIDTH), csr)) {
    tf->printf("Failed to find VM range covered by diapason [%p, %p)\n",
               va_from - PAGE_SIZE, va_from + (pages << PAGE_WIDTH));
    tf->failed();
  }  
}

static void __check_num_vmrs(test_framework_t *tf, vmm_t *vmm, uintptr_t from, uintptr_t to, int expected)
{
  int i;
  vmrange_set_t vmrs;

  vmranges_find_covered(vmm, from, to, &vmrs);
  if (!vmrs.vmr) {
    tf->printf("Can't find VM ranges covered by diapason [%p, %p)\n", from, to);
    tf->failed();
  }

  i = 0;
  while (vmrs.vmr) {
    i++;
    vmrange_set_next(&vmrs);
  }
  if (i != expected) {
    tf->printf("Unexpected number of VM ranges covered by diapason [%p, %p] (%d). %d was expected!\n",
               from, to, i, expected);
    tf->failed();
  }
}

static const int TOTAL = 128;

static void unmap_split_test(test_framework_t *tf, vmm_t *vmm)
{
  vmrange_t *vmr;
  uintptr_t start = USPACE_VA_BOTTOM + PAGE_SIZE;
  page_idx_t pgs = 1024;
  ttree_cursor_t csr;
  int total = TOTAL;
  int num = 1;

  while (total--) {
    ttree_cursor_init(&vmm->vmranges_tree, &csr);
    __unmap_chunk(tf, vmm, start, pgs, &csr);
    if (csr.state != TT_CSR_TIED) {
      tf->printf("Cursor must be in a tied state!\n");
      ttree_display_cursor_dbg(&csr);
      tf->failed();
    }
  
    vmr = ttree_item_from_cursor(&csr);
    __validate_hole_sz(tf, vmr, pgs << PAGE_WIDTH);
    if (vmr->bounds.space_end != start) {
      tf->printf("Unexpected top virtual address of range(%p). %p was expected!\n",
                 vmr->bounds.space_end, start);
      tf->failed();
    }
    if (ttree_cursor_next(&csr) < 0) {
      tf->printf("ttree_cursor_next failed!\n");
      ttree_display_cursor_dbg(&csr);
      tf->failed();
    }

    vmr = ttree_item_from_cursor(&csr);
    if (vmr->bounds.space_start != (start + (pgs << PAGE_WIDTH))) {
      tf->printf("Unexpected bottom virtual address of range(%p). %p was expected!\n",
                 vmr->bounds.space_start, start + (pgs << PAGE_WIDTH));
      tf->failed();
    }
    
    __validate_hole_sz(tf, vmr, 0);
    if (vmm->num_vmrs != ++num) {
      tf->printf("Unexpected number of VM ranges: %d. (%d was expected)!\n", vmm->num_vmrs, num);
      tf->failed();
    }

    start += (pgs + 1) << PAGE_WIDTH;
  }

  tf->printf("Unmap big chunk of address space\n");
  __check_num_vmrs(tf, vmm, USPACE_VA_BOTTOM, USPACE_VA_TOP, num);
  start += PAGE_SIZE * 2;
  ttree_cursor_init(&vmm->vmranges_tree, &csr);
  __unmap_chunk(tf, vmm, start, (USPACE_VA_TOP - start - PAGE_SIZE) >> PAGE_WIDTH, &csr);
  if (csr.state != TT_CSR_TIED) {
    tf->printf("Cursor must be in a tied state!\n");
    ttree_display_cursor_dbg(&csr);
    tf->failed();
  }

  vmr = ttree_item_from_cursor(&csr);
  __validate_hole_sz(tf, vmr, USPACE_VA_TOP - (start + PAGE_SIZE));
  if (vmr->bounds.space_end != start) {
    tf->printf("Unexpected top virtual address of range [%p, %p). Expected: %p!\n",
               vmr->bounds.space_start, vmr->bounds.space_end, start);
    tf->failed();
  }
  if (ttree_cursor_next(&csr) < 0) {
    tf->printf("Invalid cursor:\n");
    ttree_display_cursor_dbg(&csr);
    tf->failed();
  }

  vmr = ttree_item_from_cursor(&csr);
  __validate_hole_sz(tf, vmr, 0);
  if (vmr->bounds.space_start != (USPACE_VA_TOP - PAGE_SIZE)) {
    tf->printf("Unexpected bottom virtual address of VM range [%p, %p). Expected: %p\n",
               vmr->bounds.space_start, vmr->bounds.space_end, USPACE_VA_TOP - PAGE_SIZE);
    tf->failed();
  }

  __check_num_vmrs(tf, vmm, USPACE_VA_BOTTOM, USPACE_VA_TOP, num + 1);
  tf->printf("Unmap the rest of used virtual space\n");
  unmap_vmranges(vmm, USPACE_VA_BOTTOM, (USPACE_VA_TOP - USPACE_VA_BOTTOM) >> PAGE_WIDTH);
  if (vmm->num_vmrs != 0) {
    tf->printf("Unexpected number of VM ranges in a VMM: %d. 0 was expected!\n", vmm->num_vmrs);
    tf->failed();
  }
}

static void tc_vma(void *ctx)
{
  test_framework_t *tf = ctx;
  vmm_t *vmm;

  tf->printf("Creating main VMM structure\n");
  vmm_enable_verbose_dbg();
  vmm = vmm_create();
  if (!vmm) {
    tf->printf("Failed to create VMM\n");
    tf->failed();
  }

  vmm_set_name_dbg(vmm, "VMM [vmmtest]");
  tf->printf("1) Continousely create VM ranges until virtual sapce is end.\n");
  map_merge_test(tf, vmm);
  tf->printf("2) Now there is no free virtual space. So try to unmap busy chunks\n");
  unmap_split_test(tf, vmm);
  is_completed = true;
  sys_exit(0);
}

static void tc_run(test_framework_t *tf, void *unused)
{
  if (kernel_thread(tc_vma, tf, NULL)) {
    tf->printf("Can't create kernel thread!");
    tf->failed();
  }

  tf->test_completion_loop(VMA_TEST_ID, &is_completed);
}

static bool tc_initialize(void **unused)
{
  return true;
}

static void tc_deinitialize(void *unused)
{
}

testcase_t vma_testcase = {
  .id = VMA_TEST_ID,
  .initialize = tc_initialize,
  .run = tc_run,
  .deinitialize = tc_deinitialize,
};
