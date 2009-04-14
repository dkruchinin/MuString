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
#include <mm/mmpool.h>
#include <mm/vmm.h>
#include <mm/pfi.h>
#include <eza/swks.h>
#include <eza/arch/mm.h>
#include <eza/arch/ptable.h>
#include <eza/arch/bios.h>
#include <eza/arch/asm.h>

uint8_t e820count;
page_frame_t *page_frames_array = NULL;
page_idx_t num_phys_pages = 0;
uintptr_t __kernel_first_free_vaddr = p2k((uintptr_t)&_kernel_end);
uintptr_t __real_kernel_end = (uintptr_t)&_kernel_end;
uintptr_t __uspace_top_vaddr = USPACE_VA_TOP;
uintptr_t __kernel_va_base = KERNEL_BASE;
uintptr_t __utrampoline_virt = 0;

static vm_mandmap_t ident_mandmap, utramp_mandmap, swks_mandmap;

#ifndef CONFIG_IOMMU
static page_idx_t dma_pages = 0;

static inline void __determine_page_mempool(page_frame_t *pframe)
{
  mm_pool_t *pool;

  if (pframe_number(pframe) < dma_pages) {
    pool = POOL_DMA();
  }
  else {
    pool = POOL_GENERAL();
  }

  mmpool_add_page(pool, pframe);
}
#else
static inline void __determine_page_mempool(page_frame_t *pframe)
{
  mmpool_add_page(POOL_GENERAL(), pframe);
}
#endif /* CONFIG_IOMMU */

#ifdef CONFIG_DEBUG_MM
static void verify_mapping(const char *descr, uintptr_t start_addr,
                          page_idx_t num_pages, page_idx_t start_idx)
{
  page_idx_t idx = start_idx, pde_idx;
  page_idx_t end_idx = idx + num_pages;

  kprintf(" Verifying %s...", descr);
  while (idx < end_idx) {
    pde_idx = vaddr2page_idx(&kernel_rpd, start_addr);
    if (pde_idx != idx)
      goto failed;

    idx++;
    start_addr += PAGE_SIZE;
  }

  kprintf(" %*s\n", strlen(descr) + 14, "[OK]");
  return;

  failed:
  kprintf(" %*s\n", 18 - strlen(descr), "[FAILED]");
  panic("Range: %p - %p. Got idx %u, but %u was expected.",
        start_addr, start_addr + ((num_pages - 1) << PAGE_WIDTH),
        pde_idx, idx);
}
#else
#define verify_mapping(descr, start_addr, num_pages, start_idx)
#endif /* CONFIG_DEBUG_MM */


static void register_mandatory_mappings(void)
{
  memset(&ident_mandmap, 0, sizeof(ident_mandmap));
  ident_mandmap.virt_addr = 0x1000;
  ident_mandmap.phys_addr = 0x1000;
  ident_mandmap.num_pages = IDENT_MAP_PAGES - 1;
  ident_mandmap.flags = KMAP_READ;
  vm_mandmap_register(&ident_mandmap, "Identity mapping");

  memset(&utramp_mandmap, 0, sizeof(utramp_mandmap));
  __utrampoline_virt = __reserve_uspace_vregion(1);
  utramp_mandmap.virt_addr = __utrampoline_virt;
  utramp_mandmap.phys_addr = k2p((uintptr_t)__userspace_trampoline_codepage);
  utramp_mandmap.num_pages = 1;
  utramp_mandmap.flags = KMAP_READ | KMAP_EXEC;
  vm_mandmap_register(&utramp_mandmap, "Utrampoline mapping");

  memset(&swks_mandmap, 0, sizeof(swks_mandmap));
  swks_mandmap.virt_addr = __reserve_uspace_vregion(SWKS_PAGES);
  swks_mandmap.phys_addr = k2p(&swks);
  swks_mandmap.num_pages = SWKS_PAGES;
  swks_mandmap.flags = KMAP_READ;
  vm_mandmap_register(&swks_mandmap, "SWKS mapping");
}

static void scan_phys_mem(void)
{
  int idx;
  bool found;
  char *types[] = { "(unknown)", "(usable)", "(reserved)", "(ACPI reclaimable)",
                    "(ACPI NVS)", "(BAD)" };
  uintptr_t last_addr = 0;

  kprintf("E820 memory map:\n");
  for(idx = 0, found = false; idx < e820count; idx++) {
    e820memmap_t *mmap = &e820table[idx];
    uintptr_t length = ((uintptr_t)mmap->length_high << 32) | mmap->length_low;

    if ((mmap->type > 5) || (mmap->type <= 0)) {
      panic("Unknown e820 memory type [%#.8x - %#.8x]: %d",
            mmap->base_address, mmap->base_address + length, mmap->type);
    }

    kprintf("BIOS-e820: %#.8x - %#.8x %s\n",
            mmap->base_address, mmap->base_address + length, types[mmap->type]);
    if(!found && mmap->base_address == BIOS_END_ADDR && mmap->type == E820_USABLE) {
      last_addr = PAGE_ALIGN(mmap->base_address + length);
      found = true;
    }
  }

  num_phys_pages = last_addr >> PAGE_WIDTH;
  if(!found)
    panic("No valid E820 memory maps found for main physical memory area!");
  if(!num_phys_pages || (num_phys_pages < MIN_PAGES_REQUIRED))
    panic("Insufficient E820 memory map found for main physical memory area!");

#ifndef CONFIG_IOMMU
  /* Setup DMA zone. */
  dma_pages = _mb2b(16) >> PAGE_WIDTH;
#endif /* CONFIG_IOMMU */
}

static void build_page_frames_array(void)
{
  page_idx_t idx;
  int e820id = 0;
  e820memmap_t *mmap = e820table;
  uintptr_t mmap_end;
  page_frame_t *page;

  mmap_end = mmap->base_address + (((uintptr_t)(mmap->length_high) << 32) | mmap->length_low);
  for (idx = 0; idx < num_phys_pages; idx++) {
    page = &page_frames_array[idx];
    memset(page, 0, sizeof(*page));
    list_init_node(&page->node);
    page->idx = idx;
    if ((uintptr_t)pframe_phys_addr(page) > mmap_end) {
      if (e820id < e820count) {
        mmap = &e820table[++e820id];
        mmap_end = mmap->base_address + (((uintptr_t)mmap->length_high << 32) | mmap->length_low);
      }
      else {
        panic("Unexpected e820id(%d). It must me less than e820count(%d), but it's not!",
              e820id, e820count);
      }
    }
    if ((mmap->type != E820_USABLE) || is_kernel_page(page) ||
        (page->idx < LAST_BIOS_PAGE)) {
      page->flags |= PF_RESERVED;
    }

    __determine_page_mempool(page);
  }
}

/* FIXME DK: remove after debugging */
void map_kernel_area(vmm_t *vmm)
{
  pde_t *src_pml4, *dst_pml4;
  page_idx_t eidx = pde_offset2idx(KERNEL_BASE, PTABLE_LEVEL_LAST);

  src_pml4 = pde_fetch(RPD_PAGEDIR(&kernel_rpd), eidx);
  dst_pml4 = pde_fetch(RPD_PAGEDIR(&vmm->rpd), eidx);
  *dst_pml4 = *src_pml4;
}

void arch_mm_init(void)
{
  uintptr_t addr;
  scan_phys_mem();

  addr = server_get_end_phy_addr();
  if (addr) {
    __real_kernel_end = addr;
  }

  /*
   * In case when IOMMU isn't used, we have to prepare
   * DMA pages buffer(lying below 16M). Page frames array
   * firs in some DMA pages. To prevent this situation
   * we locate it after 16M.
   */
#ifdef CONFIG_IOMMU
  page_frames_array = (page_frame_t *)PAGE_ALIGN(p2k(__real_kernel_end));
#else
  page_frames_array = (page_frame_t *)p2k(_mb2b(16));
#endif /* CONFIG_IOMMU */

  __kernel_first_free_vaddr = (uintptr_t)page_frames_array +
    sizeof(page_frame_t) * num_phys_pages;
  kprintf("=> %p .. %p\n", page_frames_array, __kernel_first_free_vaddr);
  build_page_frames_array();
}

void arch_mm_remap_pages(void)
{
  int ret;

  /* Create identity mapping */
  ret = mmap_kern(0x1000, 1, IDENT_MAP_PAGES - 1,
                  KMAP_READ | KMAP_WRITE | KMAP_KERN);
  if (ret) {
    panic("Can't create identity mapping (%p -> %p)! [errcode=%d]",
          0x1000, IDENT_MAP_PAGES << PAGE_WIDTH, ret);
  }

  verify_mapping("identity mapping", 0x1000, IDENT_MAP_PAGES - 1, 1);
  ret = mmap_kern(KERNEL_BASE, 0, num_phys_pages,
                  KMAP_READ | KMAP_WRITE | KMAP_EXEC | KMAP_KERN);
  if (ret) {
    panic("Can't create kernel mapping (%p -> %p)! [errcode=%d]",
          KERNEL_BASE, num_phys_pages << PAGE_WIDTH, ret);
  }

  /* Verify that mappings are valid. */
  verify_mapping("general mapping", KERNEL_BASE, num_phys_pages, 0);

  /* Now we should register our direct mapping area and kernel area
   * to allow them be mapped as mandatory areas in user memory space.
   */
  register_mandatory_mappings();
  arch_smp_mm_init(0);
}


void arch_smp_mm_init(cpu_id_t cpu)
{
  load_cr3(pde_fetch(RPD_PAGEDIR(&kernel_rpd), 0));
}

long get_swks_virtual_address(void)
{
  return swks_mandmap.virt_addr;
}
