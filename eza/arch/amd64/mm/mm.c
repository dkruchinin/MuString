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
 * (c) Copyright 2008 Michael Tsymbalyuk <mtzaurus@gmail.com>
 * (c) Copyright 2008 Dan Kruchinin <dan.kruchinin@gmail.com>
 *
 * eza/amd64/mm/mm.c: Implementations of routines for initial memory remapping
 *                    using 4K pages (AMD64-specific).
 *
 */

#include <config.h>
#include <server.h>
#include <ds/iterator.h>
#include <ds/list.h>
#include <mlibc/assert.h>
#include <mlibc/kprintf.h>
#include <mlibc/string.h>
#include <mm/page.h>
#include <mm/pfi.h>
#include <mm/mmpool.h>
#include <mm/mmap.h>
#include <mm/vmm.h>
#include <eza/kernel.h>
#include <eza/errno.h>
#include <eza/vm.h>
#include <eza/swks.h>
#include <eza/arch/mm.h>
#include <eza/arch/ptable.h>
#include <eza/arch/mm_types.h>

/* Initial kernel top-level page directory record. */
uintptr_t _kernel_extended_end;
uint8_t e820count;
uint8_t k_entries[PAGE_SIZE] __attribute__((aligned(PAGE_SIZE)));

static page_idx_t dma_pages = 0;
static uint64_t min_phys_addr = 0, max_phys_addr = 0;

static vm_range_t direct_mapping_area;
uintptr_t kernel_min_vaddr;

#ifdef CONFIG_DEBUG_MM
static void verify_mapping(const char *descr, uintptr_t start_addr,
                          page_idx_t num_pages, page_idx_t start_idx)
{
  page_frame_iterator_t pfi;
  ITERATOR_CTX(page_frame, PF_ITER_PTABLE) pfi_ptable_ctx;

  pfi_ptable_init(&pfi, &pfi_ptable_ctx, &kernel_rpd, start_addr, num_pages);
  kprintf(" Verifying %s...", descr);
  iterate_forward(&pfi) {
    if (pfi.pf_idx != start_idx)
      goto failed;

    start_idx++;
  }
  if (pfi.error)
    goto failed;
  
  kprintf(" %*s\n", strlen(descr) + 14, "[OK]");
  return;

  failed:
  kprintf(" %*s\n", 18 - strlen(descr), "[FAILED]");
  panic("verify_mapping: Range: %p - %p. Got idx %u, but %u was expected. ERROR = %d",
        start_addr, start_addr + ((num_pages - 1) << PAGE_WIDTH),
        pfi.pf_idx, start_idx, pfi.error);
}
#else
#define verify_mapping(descr, start_addr, num_pages, start_idx)
#endif /* CONFIG_DEBUG_MM */

static void scan_phys_mem(void)
{
  int idx, found;
  kprintf( "E820 memory map:\n" ); 
  char *types[] = { "(unknown)", "(usable)", "(reserved)", "(ACPI reclaimable)",
                    "(ACPI non-volatile)", "(BAD)" };

  for( idx = 0, found = 0; idx < e820count; idx++ ) {
    e820memmap_t *mmap = &e820table[idx];
    uint64_t length = ((uintptr_t)mmap->length_high << 32) | mmap->length_low;
    char *type;

    if( mmap->type <= 5 ) {
      type = types[mmap->type];
    } else {
      type = types[0];
    }

    kprintf(" BIOS-e820: %#.8x - %#.8x %s\n",
            mmap->base_address, mmap->base_address + length, type);

    if( !found && mmap->base_address == KERNEL_PHYS_START && mmap->type == 1 ) {
      min_phys_addr = 0;
      max_phys_addr = mmap->base_address + length;
      found = 1;
    }
  }

  if( !found ) {
    panic( "detect_physical_memory(): No valid E820 memory maps found for main physical memory area !\n" );
  }

  if( max_phys_addr <= min_phys_addr ||
      ((min_phys_addr - max_phys_addr) <= MIN_PHYS_MEMORY_REQUIRED )) {
    panic( "detect_physical_memory(): Insufficient E820 memory map found for main physical memory area !\n" );
  }

#ifndef CONFIG_IOMMU
  /* Setup DMA zone. */
  dma_pages = _mb2b(16) >> PAGE_WIDTH;
#endif /* CONFIG_IOMMU */
}

static void __extend_kernel_end(void)
{
   uintptr_t addr = server_get_end_phy_addr();
   if (!addr) {
      _kernel_extended_end = (uintptr_t)PAGE_ALIGN(KERNEL_FIRST_ADDRESS);
      return;
   }

   _kernel_extended_end = (uintptr_t)PAGE_ALIGN(p2k_code(addr));
}

page_idx_t mm_vaddr2page_idx(rpd_t *rpd, uintptr_t vaddr)
{
  uintptr_t va = PAGE_ALIGN_DOWN(vaddr);
  page_frame_t *cur_dir = rpd->pml4;
  pde_t *pde;
  int level;

  for (level = PTABLE_LEVEL_LAST; level > PTABLE_LEVEL_FIRST; level--) {
    pde = pde_fetch(cur_dir, vaddr2pde_idx(va, level));
    if (!(pde->flags & PDE_PRESENT))
      return PAGE_IDX_INVAL;

    cur_dir = pde_fetch_subdir(pde);
  }

  pde = pde_fetch(cur_dir, vaddr2pde_idx(va, PTABLE_LEVEL_FIRST));
  if (!(pde->flags & PDE_PRESENT))
    return PAGE_IDX_INVAL;

  return pde_fetch_page_idx(pde);
}

void arch_mm_init(void)
{
  CT_ASSERT((sizeof(pdir_level_t) + sizeof(pde_idx_t)) <= sizeof(atomic_t));
  kprintf("[MM] Scanning physical memory...\n");
  scan_phys_mem();
  swks.mem_total_pages = max_phys_addr >> PAGE_WIDTH;
  __extend_kernel_end();
  page_frames_array = KERNEL_FIRST_FREE_ADDRESS;  
  _kernel_extended_end =
    PAGE_ALIGN((uintptr_t)page_frames_array + sizeof(page_frame_t) * swks.mem_total_pages);
  kprintf(" Scanned: %ldM, %ld pages\n", (long)_b2mb(max_phys_addr - min_phys_addr),
          (long)swks.mem_total_pages);
}

static int prepare_page(page_idx_t idx, ITERATOR_CTX(page_frame, PF_ITER_ARCH) *ctx)
{
  e820memmap_t *mmap = ctx->mmap;
  uintptr_t mmap_end = mmap->base_address +
    (((uintptr_t)(mmap->length_high) << 32) | mmap->length_low);
  uint32_t mmap_type = mmap->type;
  page_frame_t *page = page_frames_array + idx;

  memset(page, 0, sizeof(*page));
  page->idx = idx;
  if (page->idx < dma_pages)
    page->flags = PF_PDMA;
  else
    page->flags = PF_PGEN;
  if ((uintptr_t)pframe_phys_addr(page) > mmap_end) { /* switching to the next e820 map */
    if (ctx->e820id < e820count) {
      ctx->mmap = mmap = &e820table[++ctx->e820id];
      mmap_end = mmap->base_address + (((uintptr_t)mmap->length_high << 32) | mmap->length_low);
      mmap_type = mmap->type;
    }
    else { /* it seems that we've received a page with invalid idx... */
      return -1;
    }
  }
  if ((mmap_type != E820_USABLE) || is_kernel_addr(pframe_to_virt(page)) ||
      (page->idx < LAST_BIOS_PAGE)) {
    page->flags |= PF_RESERVED;
  }
  
  return 0;
}

static void __pfiter_first(page_frame_iterator_t *pfi)
{
  ITERATOR_CTX(page_frame, PF_ITER_ARCH) *ctx;

  ITER_DBG_CHECK_TYPE(pfi, PF_ITER_ARCH);
  pfi->pf_idx = 0;
  pfi->state = ITER_RUN;
  ctx = iter_fetch_ctx(pfi);
  ctx->mmap = &e820table[0];
  ctx->e820id = 0;
  if (prepare_page(pfi->pf_idx, ctx) < 0) {
    panic("e820 error: Can't recognize a page with index %d and physical address %p\n",
          pfi->pf_idx, pframe_phys_addr(page_frames_array + pfi->pf_idx));
  }
}

static void __pfiter_next(page_frame_iterator_t *pfi)
{
  ITERATOR_CTX(page_frame, PF_ITER_ARCH) *ctx;

  ITER_DBG_CHECK_TYPE(pfi, PF_ITER_ARCH);
  if (pfi->pf_idx >= swks.mem_total_pages) {
    pfi->state = ITER_STOP;
    pfi->pf_idx = PAGE_IDX_INVAL;
  }
  else {
    ctx = iter_fetch_ctx(pfi);
    pfi->pf_idx++;
    if (prepare_page(pfi->pf_idx, ctx) < 0) {
      panic("e820 error: Can't recognize a page with index %d and physical address %p\n",
          pfi->pf_idx, pframe_phys_addr(page_frames_array + pfi->pf_idx));
    }
  }
}


void pfi_arch_init(page_frame_iterator_t *pfi,
                   ITERATOR_CTX(page_frame, PF_ITER_ARCH) *ctx)
{
  static bool __arch_iterator_init = false;

  if (__arch_iterator_init)
    panic("pfi_arch_init: Platform-specific page frame iterator may be initialized only one time!");
  
  pfi->first = __pfiter_first;
  pfi->next = __pfiter_next;
  pfi->last = pfi->prev = NULL;
  pfi->pf_idx = PAGE_IDX_INVAL;
  pfi->error = 0;
  iter_init(pfi, PF_ITER_ARCH);
  memset(ctx, 0, sizeof(*ctx));
  iter_set_ctx(pfi, ctx);
  __arch_iterator_init = true;
}

void arch_mm_remap_pages(void)
{
  int ret;
  mmap_info_t minfo;
  page_frame_iterator_t pfi;
  ITERATOR_CTX(page_frame, PF_ITER_INDEX) pfi_index_ctx;

  /* Create identity mapping */
  minfo.va_from = 0x1000;
  minfo.va_to = minfo.va_from + ((IDENT_MAP_PAGES - 1) << PAGE_WIDTH);
  minfo.ptable_flags = PDE_RW;
  pfi_index_init(&pfi, &pfi_index_ctx, 1, IDENT_MAP_PAGES - 1);
  iter_first(&pfi);
  minfo.pfi = &pfi;
  ret = ptable_map(&kernel_rpd, &minfo);
  if (ret) {
    panic("arch_mm_remap_pages(): Can't create identity mapping (%p -> %p)! [errcode=%d]",
          0x1000, IDENT_MAP_PAGES << PAGE_WIDTH, ret);
  }

  verify_mapping("identity mapping", 0x1000, IDENT_MAP_PAGES - 1, 1);
  
  /* Now we should remap all available physical memory starting at 'KERNEL_BASE'. */
  minfo.va_from = KERNEL_BASE;
  minfo.va_to = minfo.va_from + (swks.mem_total_pages << PAGE_WIDTH);
  minfo.ptable_flags = PDE_RW;
  pfi_index_init(&pfi, &pfi_index_ctx, 0, swks.mem_total_pages);
  iter_first(&pfi);
  minfo.pfi = &pfi;
  ret = ptable_map(&kernel_rpd, &minfo);
  if (ret)
    panic("arch_mm_remap_pages(): Can't remap physical pages !");

  /* Verify that mappings are valid. */  
  verify_mapping("general mapping", KERNEL_BASE, swks.mem_total_pages, 0);

  /* Now we should register our direct mapping area and kernel area
   * to allow them be mapped as mandatory areas in user memory space.
   */
  direct_mapping_area.phys_addr=0x1000;
  direct_mapping_area.virt_addr=0x1000;
  direct_mapping_area.num_pages=IDENT_MAP_PAGES - 1;
  direct_mapping_area.map_proto = PROT_READ | PROT_WRITE;
  direct_mapping_area.map_flags= MAP_FIXED;
  vm_register_user_mandatory_area(&direct_mapping_area);

  /* TODO: [mt] redesign 'kernel_min_vaddr'. */
  kernel_min_vaddr=KERNEL_BASE;

  /* All CPUs must initially reload their CR3 registers with already
   * initialized Level-4 page directory.
   */
  arch_smp_mm_init(0);  
}

/* FIXME DK: move this fucking stuff to another(better) place... */
status_t arch_vm_map_kernel_area(task_t *task)
{
  pde_t *src_pml4, *dst_pml4;
  pde_idx_t eidx = vaddr2pde_idx(KERNEL_BASE, PTABLE_LEVEL_LAST);

  src_pml4 = pde_fetch(kernel_rpd.pml4, eidx);
  dst_pml4 = pde_fetch(task->rpd.pml4, eidx);

  /* Just copy PML4 entry from kernel page directoy to user one. */
  *dst_pml4 = *src_pml4;

  return 0;
}

void arch_smp_mm_init(int cpu)
{  
  load_cr3(_k2p((uintptr_t)pframe_to_virt(kernel_rpd.pml4)), 1, 1);
}
