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
 * (c) Copyright 2008 Dan Kruchinin <dk@jarios.org>
 *
 */

#include <config.h>
#include <server.h>
#include <arch/init.h>
#include <arch/ptable.h>
#include <arch/cpu.h>
#include <arch/msr.h>
#include <arch/mem.h>
#include <arch/cpufeatures.h>
#include <mm/page.h>
#include <mm/mem.h>
#include <mm/mmpool.h>
#include <mm/ealloc.h>
#include <mm/vmm.h>
#include <sync/spinlock.h>
#include <mstring/swks.h>
#include <mstring/kprintf.h>
#include <mstring/stddef.h>
#include <mstring/bitwise.h>
#include <mstring/string.h>
#include <mstring/panic.h>
#include <mstring/errno.h>
#include <mstring/types.h>

multiboot_info_t *mb_info;
page_idx_t num_phys_pages;
uintptr_t __kernel_end;
uintptr_t __utrampoline_virt;

static SPINLOCK_DEFINE(vregion_lock);
static uintptr_t vregion_cur_ptr = KERNEL_OFFSET;
static vm_mandmap_t ident_mandmap, utramp_mandmap, swks_mandmap;

/*
 * Points to the very first available page-aligned address.
 * Before high-level page allocator is initialized, pages
 * will be allocated starting from ptable_mem_start address.
 */
static INITDATA char *ptable_mem_start;

/* Total amount of free space. */
static INITDATA ulong_t space_rest;

/* The very last address marked as available for use by E820 */
static INITDATA uintptr_t last_usable_addr;

#define SET_KERNEL_END(addr)                    \
  (__kernel_end = (addr))

#define LAST_BIOS_PAGE (MB2B(1) >> PAGE_WIDTH)

/* Get pointer to the next item in E820 memory map */
#define E820_MMAP_NEXT(mmap)                    \
  (e820memmap_t *)((uintptr_t)(mmap) + (mmap)->size + sizeof((mmap)->size))

#ifdef CONFIG_DEBUG
static void INITCODE verify_mapping(const char *descr, page_idx_t pidx,
                                        page_idx_t npages, uintptr_t addr)
{
  page_idx_t out_idx = 0;

  kprintf(KO_DEBUG "Verifying %s... ", descr);
  while (pidx < npages) {
    out_idx = pt_ops.vaddr_to_pidx(&kernel_root_pdir, addr, NULL);
    if (pidx != out_idx) {
      goto failed;
    }

    pidx++;
    addr += PAGE_SIZE;
  }

  kprintf("%*s\n", 24 - strlen(descr), "[OK]");
  return;

failed:
  kprintf("%*s\n", 24 - strlen(descr), "[FAILED]");
  panic("Range: %p - %p. Got idx %u, but %u was expected.",
        addr, addr + ((uintptr_t)(npages - 1) << PAGE_WIDTH),
        out_idx, pidx);
}
#else /* !CONFIG_DEBUG */
#define verify_mapping(descr, pidx, npages, addr)
#endif /* CONFIG_DEBUG */

/* Enable NXE CPU feature if available. */
static INITCODE void enable_nx(void)
{
  if (cpu_has_feature(AMD64_FTR_NX)) {
    __ptbl_allowed_flags_mask |= PDE_NX;
    efer_set_feature(EFER_NXE);
  }
}

static INITCODE void register_mandatory_mappings(void)
{
  memset(&ident_mandmap, 0, sizeof(ident_mandmap));
  ident_mandmap.virt_addr = 0x1000;
  ident_mandmap.phys_addr = 0x1000;
  ident_mandmap.num_pages = IDENT_MAP_PAGES - 1;
  ident_mandmap.flags = KMAP_READ | KMAP_KERN;
  vm_mandmap_register(&ident_mandmap, "Identity mapping");

  memset(&utramp_mandmap, 0, sizeof(utramp_mandmap));
  __utrampoline_virt = USPACE_VADDR_TOP + PAGE_SIZE;//__reserve_uspace_vregion(1);
  utramp_mandmap.virt_addr = __utrampoline_virt;
  utramp_mandmap.phys_addr = KVIRT_TO_PHYS((uintptr_t)__userspace_trampoline_codepage);
  utramp_mandmap.num_pages = 1;
  utramp_mandmap.flags = KMAP_READ | KMAP_EXEC;
  vm_mandmap_register(&utramp_mandmap, "Utrampoline mapping");

  memset(&swks_mandmap, 0, sizeof(swks_mandmap));
  swks_mandmap.virt_addr = USPACE_VADDR_TOP + PAGE_SIZE + (SWKS_PAGES * PAGE_SIZE);//__reserve_uspace_vregion(SWKS_PAGES);
  swks_mandmap.phys_addr = KVIRT_TO_PHYS(&swks);
  swks_mandmap.num_pages = SWKS_PAGES;
  swks_mandmap.flags = KMAP_READ;
  vm_mandmap_register(&swks_mandmap, "SWKS mapping");
}

static INITCODE void *boottime_alloc_pdir(void)
{
  void *ret;

  ret = ealloc_page();
  if (!ret) {
    ealloc_dump();
    panic("Failed to allocate page for page directory using ealloc!");
  }

  memset(ret, 0, PAGE_SIZE);
  return ret;
}

static INITCODE void boottime_free_pdir(void *pdir)
{
}

static int root_pdir_init_arch(rpd_t *rpd)
{
  rpd->root_dir = pt_ops.alloc_pagedir();
  if (!rpd->root_dir) {
    return ERR(-ENOMEM);
  }

  return 0;
}

static void root_pdir_deinit_arch(rpd_t *rpd)
{
  ASSERT(rpd->root_dir != NULL);
  pt_ops.free_pagedir(rpd->root_dir);
}

/*
 * alloc_pagedir and free_pagedir methods will be
 * replaced after high-level page allocator will be
 * initialized.
 */
struct pt_ops pt_ops = {
  .map_page = ptable_map_page,
  .unmap_page = ptable_unmap_page,
  .vaddr_to_pidx = ptable_vaddr_to_pidx,
  .alloc_pagedir = boottime_alloc_pdir,
  .free_pagedir = boottime_free_pdir,
  .root_pdir_init_arch = root_pdir_init_arch,
  .root_pdir_deinit_arch = root_pdir_deinit_arch,
};

static inline bool is_kernel_page(page_frame_t *page)
{
  return ((uintptr_t)pframe_to_phys(page) < KERNEL_END_PHYS);
}

static INITCODE void __map_kmem(page_idx_t pidx, page_idx_t npages,
                                    uintptr_t addr, ptable_flags_t flags)
{
  int ret = 0;

  while (pidx < npages) {
    ret = pt_ops.map_page(&kernel_root_pdir, addr, pidx, flags);
    if (ret) {
      panic("Failed to create kernel mapping: %p -> %p. [ERR = %d]\n",
            (uintptr_t)pidx << PAGE_WIDTH, addr);
    }

    pidx++;
    addr += PAGE_SIZE;
  }
}

static INITCODE void remap_kernel_mem(void)
{
  page_idx_t num_pages, nc_pages;

  /* Create identity mapping */
  __map_kmem(1, IDENT_MAP_PAGES, 0x1000, PDE_RW | PDE_PCD);
  verify_mapping("Identity mapping", 1, IDENT_MAP_PAGES, 0x1000);

  num_pages = num_phys_pages;
  if (num_pages > MAX_PAGES_MAP_FIRST) {
    num_pages = MAX_PAGES_MAP_FIRST;
  }

  /* Create main kernel_mapping */
  nc_pages = MB2B(1) >> PAGE_WIDTH; /* number of non-cachable pages */
  __map_kmem(0, nc_pages, KERNEL_OFFSET, PDE_RW | PDE_PCD);
  __map_kmem(nc_pages, num_pages,
             KERNEL_OFFSET + MB2B(1), PDE_RW);
  verify_mapping("Kernel mapping", 0, num_pages, KERNEL_OFFSET);
}

static INITCODE void paging_init(void)
{
  __ptbl_allowed_flags_mask = PDE_RW | PDE_US |
    PDE_PWT | PDE_PS | PDE_GLOBAL | PDE_PAT;

  remap_kernel_mem();
  arch_cpu_enable_paging();
  if (num_phys_pages > MAX_PAGES_MAP_FIRST) {
    uintptr_t start_vaddr;

    start_vaddr = PHYS_TO_KVIRT((uintptr_t)MAX_PAGES_MAP_FIRST << PAGE_WIDTH);
    __map_kmem(MAX_PAGES_MAP_FIRST, num_phys_pages,
               start_vaddr, PDE_RW);
    verify_mapping("Kerenl high mapping", MAX_PAGES_MAP_FIRST,
                   num_phys_pages, start_vaddr);
  }
}

static INITCODE void scan_phys_mem(void)
{
  e820memmap_t *mmap, *e820_end;
  char *mem_types[] = {
    "(unknown)", "(usable)", "(reserved)",
    "(ACPI reclaimable)", "(ACPI NVS)", "(BAD)"
  };

  if (!bit_test(&mb_info->flags, 6)) {
    panic("E820 memory map is invalid!\n"
          "mmap_addr = %p, mmap_length = %#x\n",
          mb_info->mmap_addr, mb_info->mmap_length);
  }

  kprintf("E820 memory map:\n");
  mmap = (e820memmap_t *)((ulong_t)mb_info->mmap_addr);
  e820_end = (e820memmap_t *)
    ((uintptr_t)mb_info->mmap_addr + mb_info->mmap_length);

  while (mmap < e820_end) {
   if ((mmap->type > 5) || (mmap->type <= 0)) {
      panic("Unknown e820 memory type [%#.10x - %#.10x]: %d",
            mmap->base_address, mmap->base_address + mmap->length, mmap->type);
    }

    kprintf("BIOS-e820: %#.10lx - %#.10lx %s\n", mmap->base_address,
            mmap->base_address + mmap->length, mem_types[mmap->type]);
    if (mmap->type == E820_USABLE) {
      last_usable_addr = mmap->base_address + mmap->length;
    }

    mmap = E820_MMAP_NEXT(mmap);
  }
  if (!last_usable_addr) {
    panic("Can not determine amount of available physical memory "
          "via E820 memory map!\n");
  }

  num_phys_pages = last_usable_addr >> PAGE_WIDTH;
  kprintf(KO_INFO "Available RAM: %dMb\n", B2MB(last_usable_addr));
}

static INITCODE void build_page_frames_array(void)
{
  page_idx_t pidx = 0;
  e820memmap_t *mmap;
  page_frame_t *page;
  int reserved;
  uintptr_t end;

  mmap = (e820memmap_t *)((uintptr_t)mb_info->mmap_addr);
  while (mmap->base_address < last_usable_addr) {
    reserved = 0;
    end = mmap->base_address + mmap->length;
    if (mmap->type != E820_USABLE) {
      reserved++;
    }

    while ((uintptr_t)pframe_id_to_phys(pidx) < end) {
      page = &page_frames_array[pidx++];
      memset(page, 0, sizeof(*page));
      if (reserved || is_kernel_page(page)) {
        page->flags = PF_RESERVED;
      }

      mmpools_register_page(page);
    }

    mmap = E820_MMAP_NEXT(mmap);
  }

  ASSERT(num_phys_pages == pidx);
}

INITCODE void arch_mem_init(void)
{
  ulong_t phys_mem_bytes = KB2B(mb_info->mem_upper + 1024);
  uintptr_t srv_addr;

  arch_register_mmpools();
  if (phys_mem_bytes < MIN_MEM_REQUIRED) {
    panic("Mstring kernel launches on systems with at least %dM of RAM. "
          "Your system has only %dM\n", B2MB(MIN_MEM_REQUIRED),
          B2MB(phys_mem_bytes) + 1);
  }

  /*
   * Scan available memory using E820 memory map.
   * Determine how much physical memory is available and
   * how much is usable.
   */
  SET_KERNEL_END(PAGE_ALIGN((uintptr_t)&_kernel_end));
  scan_phys_mem();

  kprintf(KO_INFO "Kernel size: %ldK\n",
          B2KB(KERNEL_END_PHYS) - 1024);

  srv_addr = server_ops->get_end_addr();
  if (srv_addr) {
    kprintf(KO_INFO "Services size: %ldK\n",
            B2KB(srv_addr - KERNEL_END_PHYS));
    SET_KERNEL_END(PAGE_ALIGN(PHYS_TO_KVIRT(srv_addr)));
  }

  /* Find out an address where pages can be allocated from */
  space_rest = phys_mem_bytes - PAGE_ALIGN(KERNEL_END_PHYS) - 1024;
  ptable_mem_start =  (char *)KERNEL_END_VIRT;

  /*
   * Initialize early memory allocator for page allocation
   * during system boot process.
   */
  ealloc_init(KERNEL_END_VIRT, space_rest);

  /* Initialize kernel root page directory(PML4) */
  initialize_rpd(&kernel_root_pdir, NULL);
  paging_init();

  /*
   * After kerenl mapping was sucessfully configured,
   * ealloc page allocator can be disabled.
   */
  ealloc_disable_feature(EALLOCF_APAGES);
  SET_KERNEL_END((uintptr_t)ealloc_data.pages);
  page_frames_array = (page_frame_t *)KERNEL_END_VIRT;
  SET_KERNEL_END(PAGE_ALIGN((uintptr_t)page_frames_array +
                            sizeof(page_frame_t) * num_phys_pages));
  build_page_frames_array();

  kprintf(KO_INFO "Page frames array size: %dK\n",
          B2KB(KERNEL_END_VIRT - (uintptr_t)page_frames_array));
  register_mandatory_mappings();
  arch_configure_mmpools();
}

INITCODE void arch_cpu_enable_paging(void)
{
  void *pml4_base = KERNEL_ROOT_PDIR()->root_dir;

  write_cr3(KVIRT_TO_PHYS(pml4_base));
  enable_nx();
}

/* FIXME DK: map_kernel_area shuuldn't be here
 * It's better to use the following code during arch-dependent
 * task configuration.
 */
void arch_smp_mm_init(cpu_id_t cpu)
{
  arch_cpu_enable_paging();
}

#include <mm/vmm.h>
void map_kernel_area(struct __vmm *vmm)
{
  pde_t *src_pml4, *dst_pml4;
  page_idx_t eidx = pde_offset2idx(KERNEL_OFFSET, PTABLE_LEVEL_LAST);

  src_pml4 = pde_fetch(ROOT_PDIR_PAGE(KERNEL_ROOT_PDIR()), eidx);
  dst_pml4 = pde_fetch(ROOT_PDIR_PAGE(&vmm->rpd), eidx);
  *dst_pml4 = *src_pml4;
}

long get_swks_virtual_address(void)
{
  return swks_mandmap.virt_addr;
}


/* End of garbage */

uintptr_t __allocate_vregion(page_idx_t num_pages)
{
  uintptr_t retaddr;

  spinlock_lock(&vregion_lock);
  retaddr = vregion_cur_ptr - ((uintptr_t)num_pages << 12);;
  if (unlikely(retaddr < USPACE_VADDR_TOP)) {
    retaddr = 0;
  }
  else {
    vregion_cur_ptr = retaddr;
  }

  spinlock_unlock(&vregion_lock);

  return retaddr;
}

ptable_flags_t kmap_to_ptable_flags(ulong_t kmap_flags)
{
  ptable_flags_t pt_flags;

  pt_flags =  !!(kmap_flags & KMAP_WRITE) << BITNUM(PDE_RW);
  pt_flags |= !(kmap_flags & KMAP_KERN) << BITNUM(PDE_US);
  pt_flags |= !!(kmap_flags & KMAP_NOCACHE) << BITNUM(PDE_PCD);
  pt_flags |= !(kmap_flags & KMAP_EXEC) << BITNUM(PDE_NX);

  return pt_flags;
}

uint32_t ptable_to_kmap_flags(ptable_flags_t flags)
{
  uint32_t kmap_flags = KMAP_READ;

  kmap_flags |= (!!(flags & PDE_RW) << BITNUM(KMAP_WRITE));
  kmap_flags |= (!(flags & PDE_US) << BITNUM(KMAP_KERN));
  kmap_flags |= (!!(flags & PDE_PCD) << BITNUM(KMAP_NOCACHE));
  kmap_flags |= (!(flags & PDE_NX) << BITNUM(KMAP_EXEC));

  return kmap_flags;
}
