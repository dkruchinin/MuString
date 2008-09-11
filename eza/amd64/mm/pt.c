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
 *
 * eza/amd64/mm/pt.c: Contains implementations of AMD64-specific routines for
 *                    manipulating page tables in Long mode.
 *
 */

#include <mm/mm.h>
#include <mm/pt.h>
#include <eza/errno.h>
#include <eza/arch/page.h>
#include <eza/arch/types.h>
#include <mm/pagealloc.h>

#define PTE_ENTRIES_PER_PAGE  512

/* Range of one entry in each page translation level. */
#define L1_ENTRY_RANGE  PAGE_SIZE
#define L2_ENTRY_RANGE  (uintptr_t)(L1_ENTRY_RANGE * PTE_ENTRIES_PER_PAGE)
#define L3_ENTRY_RANGE  (uintptr_t)(L2_ENTRY_RANGE * PTE_ENTRIES_PER_PAGE)
#define L4_ENTRY_RANGE  (uintptr_t)(L3_ENTRY_RANGE * PTE_ENTRIES_PER_PAGE)

/* Values for masking per-level address ranges. */
#define L2_ADDR_MASK  ~(L2_ENTRY_RANGE-1)
#define L3_ADDR_MASK  ~(L3_ENTRY_RANGE-1)
#define L4_ADDR_MASK  ~(L4_ENTRY_RANGE-1)

typedef struct __pml4_entry {
  unsigned present: 1;
  unsigned rw: 1;
  unsigned us: 1;
  unsigned pwt: 1;
  unsigned pcd: 1;
  unsigned a:1;
  unsigned reserved_6_8: 3;
  unsigned available_9_11: 3;
  unsigned base_0_19: 20;
  unsigned base_20_39: 20;
  unsigned available_52_62: 11;
  unsigned nx: 1;
} pml4_entry_t;

typedef pml4_entry_t pdp3_entry_t;

typedef struct __pde2_entry {
  unsigned present: 1;
  unsigned rw: 1;
  unsigned us: 1;
  unsigned pwt: 1;
  unsigned pcd: 1;
  unsigned a:1;
  unsigned x6: 1;
  unsigned page_size: 1; /* 0: 4K pages, 1: 2Mb pages */
  unsigned x8: 1;
  unsigned available_9_11: 3;
  unsigned base_0_19: 20;
  unsigned base_20_39: 20;
  unsigned available_52_62: 11;
  unsigned nx: 1;
} pde2_entry_t;

#define tlb_entry_valid(e) \
    (e->present != 0)

static inline pml4_entry_t *vaddr_to_pml4(uintptr_t vaddr,page_directory_t *pd)
{
  register uintptr_t idx = (vaddr >> 39) & 0x1FF;
  return ((pml4_entry_t *)pd->entries) + idx;
}

static int populate_pdp3_entry(pdp3_entry_t *entry, page_frame_accessor_t *pacc, page_flags_t flags,
                               void *pacc_ctx)
{  
  page_frame_t *pf = pacc->alloc_page(pacc_ctx,flags,1);
  if( pf != NULL ) {
    register uintptr_t pfn = pframe_number(pf);

    entry->present = 1;
    entry->rw = 1;
    entry->us = 0;
    entry->pwt = 0;
    entry->pcd = 0;
    entry->a = 0;
    entry->reserved_6_8 = 0;
    entry->available_9_11 = 0;
    entry->available_52_62 = 0;
    entry->nx = 0;

    entry->base_0_19 = pfn & 0xfffff;
    entry->base_20_39 = (pfn >> 20) & 0xfffff;

    return 0;
  }
  return -ENOMEM;
}

static int populate_pde2_entry(pde2_entry_t *entry, page_frame_accessor_t *pacc,
                               page_flags_t flags, void *pacc_ctx)
{
  page_frame_t *pf = pacc->alloc_page(pacc_ctx,flags,1);
  if( pf != NULL ) {
    register uintptr_t pfn = pframe_number(pf);

    entry->present = 1;
    entry->rw = 1;
    entry->us = 0;
    entry->pwt = 0;
    entry->pcd = 0;
    entry->a = 0;
    entry->x6 = 0;
    entry->page_size = 0; /* 4K pages */
    entry->x8 = 0;
    entry->available_52_62 = 0;
    entry->nx = 0;

    entry->base_0_19 = pfn & 0xfffff;
    entry->base_20_39 = (pfn >> 20) & 0xfffff;

    return 0;
  }
  return -ENOMEM;
}

static int map_pde2_range( pde2_entry_t *pde2, uintptr_t virt_addr, uintptr_t end_addr,
                           page_frame_accessor_t *pacc, page_flags_t flags, void *pacc_ctx ) {
  register uintptr_t v = (pde2->base_0_19 | (pde2->base_20_39 << 20)) << 12; /* Get base address of PTE */

  do {
    pte_t *pte = (pte_t *)p2k_code(v) + ((virt_addr >> 12) & 0x1ff);
    page_idx_t frame_idx = pacc->next_frame(pacc_ctx);
    register uintptr_t page_base = frame_idx;

    pte->present = 1;
    pte->rw = 1;

    /* User/supervisor ? */
    if( flags & PF_KERNEL_PAGE ) {
      pte->us = 0;
    } else {
      pte->us = 1;
    }

    pte->pwt = 0;

    /* Disable caching for I/O regions. */
    if( flags & PF_IO_PAGE ) {
      pte->pcd = 1;
    } else {
      pte->pcd = 0;
    }

    pte->a = 0;
    pte->d = 0;
    pte->pat = 0;
    pte->g = 1;
    pte->available_9_11= 0;
    pte->available_52_62 = 0;
    pte->nx = 0;

    pte->base_0_19 = page_base & 0xfffff;
    pte->base_20_39 = (page_base >> 20) & 0xfffff;
    virt_addr += L1_ENTRY_RANGE;
 
  } while(virt_addr < end_addr);

  return 0;
}

static int map_pdp3_range(pdp3_entry_t *pdp3, uintptr_t virt_addr, uintptr_t end_addr,
                           page_frame_accessor_t *pacc, page_flags_t flags,void *pacc_ctx )
{
  register uintptr_t v = (pdp3->base_0_19 | (pdp3->base_20_39 << 20)) << 12; /* Get base address of PDT */
  pde2_entry_t *pde2;
  register uintptr_t length = end_addr - virt_addr;
  register uintptr_t to_addr;

  v = p2k_code(v);

  pde2 = (pde2_entry_t *)v + ((virt_addr >> 21) & 0x1ff);

  do {
     if( !tlb_entry_valid(pde2) ) {
      if( populate_pde2_entry(pde2,pacc,flags,pacc_ctx) != 0 ) {
        return -ENOMEM;
      }
    }

     to_addr = virt_addr + length;
    if( to_addr > ((virt_addr + L2_ENTRY_RANGE) & L2_ADDR_MASK) ) {
      to_addr = ((virt_addr + L2_ENTRY_RANGE) & L2_ADDR_MASK);
    }

    length -= (to_addr - virt_addr);

    if( map_pde2_range(pde2,virt_addr,to_addr,pacc,flags,pacc_ctx) != 0 ) {
      return -ENOMEM;
    }

     virt_addr = to_addr;
    pde2++;
  } while(virt_addr < end_addr);

  return 0;
}


static int map_pml4_range(pml4_entry_t *pml4, uintptr_t virt_addr, uintptr_t end_addr,
                           page_frame_accessor_t *pacc, page_flags_t flags,void *pacc_ctx)
{
  register uintptr_t v = (pml4->base_0_19 | (pml4->base_20_39 << 20)) << 12; /* Get base address of PDP */
  pdp3_entry_t *pdp3;
  register uintptr_t length = end_addr - virt_addr;
  register uintptr_t to_addr;

  pdp3 = (pdp3_entry_t *)p2k_code(v) + ((virt_addr >> 30) & 0x1ff); 

  do {
    if( !tlb_entry_valid(pdp3)) {
      if( populate_pdp3_entry( pdp3,pacc,flags,pacc_ctx) != 0) {
        return -ENOMEM;
      }
    }

    to_addr = virt_addr + length;
    if( to_addr > ((virt_addr + L3_ENTRY_RANGE) & L3_ADDR_MASK) ) {
      to_addr = ((virt_addr + L3_ENTRY_RANGE) & L3_ADDR_MASK);
    }

    length -= (to_addr - virt_addr);
    if( map_pdp3_range(pdp3,virt_addr,to_addr,pacc,flags,pacc_ctx) != 0) {
      return -ENOMEM;
    }
    virt_addr = to_addr;
    pdp3++;
  } while(virt_addr < end_addr);
  return 0;
}

static int populate_pml4_entry(pml4_entry_t *entry, page_frame_accessor_t *pacc,
                               page_flags_t flags, void *pacc_ctx)
{  
  page_frame_t *pf = pacc->alloc_page(pacc_ctx,flags,1);
  if( pf != NULL ) {
    register uintptr_t pfn = pframe_number(pf);

    entry->present = 1;
    entry->rw = 1;
    entry->us = 0;
    entry->pwt = 0;
    entry->pcd = 0;
    entry->a = 0;
    entry->reserved_6_8 = 0;
    entry->available_9_11 = 0;
    entry->available_52_62 = 0;
    entry->nx = 0;

    entry->base_0_19 = pfn & 0xfffff;
    entry->base_20_39 = (pfn >> 20) & 0xfffff;

    return 0;
  }
  return -ENOMEM;
}

int mm_map_pages( page_directory_t *pd, page_frame_accessor_t *pacc, uintptr_t virt_addr,
                  size_t num_pages, page_flags_t flags, void *pacc_ctx ) {
  register uintptr_t end_addr, length, to_addr;

  if( pd == NULL || pd->entries == NULL || num_pages == 0 ||
      virt_addr == 0 || pacc == NULL ) {
    return -EINVAL;
  }

  if( ((uintptr_t)pd->entries & ~PAGE_ADDR_MASK) != 0 ) {
    return -EINVAL;
  }

  end_addr = virt_addr + num_pages * PAGE_SIZE;
  if(end_addr < virt_addr || end_addr > MAX_VIRT_ADDRESS ) { /* Overflow */
    return -EINVAL;
  }

  virt_addr &= PAGE_ADDR_MASK;

  /* OK, lets map it. */
  pml4_entry_t *pml4 = vaddr_to_pml4(virt_addr,pd);
  length = end_addr - virt_addr;

  do {
    if( !tlb_entry_valid(pml4)) {
      /* No PML4 entry, so we should instantiate a new one. */
      if( populate_pml4_entry(pml4,pacc,flags,pacc_ctx) != 0 ) {
        return -ENOMEM;
      }
    }
    
    to_addr = virt_addr + length;

    /* Make sure we are not inside the last L4 entry. */
    if( (virt_addr < (MAX_VIRT_ADDRESS - L4_ENTRY_RANGE )) &&
        (to_addr > ((virt_addr + L4_ENTRY_RANGE) & L4_ADDR_MASK) ) ) {
      to_addr = ((virt_addr + L4_ENTRY_RANGE) & L4_ADDR_MASK);
    }

    length -= (to_addr - virt_addr);
    if( map_pml4_range(pml4,virt_addr,to_addr,pacc,flags,pacc_ctx) != 0 ) {
      return -ENOMEM;
    }

    pml4++;
    virt_addr = to_addr;
    break;
  } while(length > L4_ENTRY_RANGE);

  return 0;
}

page_idx_t mm_pin_virtual_address( page_directory_t *pd, uintptr_t virt_addr )
{
  page_idx_t idx = INVALID_PAGE_IDX;
  pml4_entry_t *pml4 = vaddr_to_pml4(virt_addr,pd);
  pdp3_entry_t *pdp3;
  register uintptr_t v;

  if( tlb_entry_valid(pml4) ) {
    v = p2k_code((pml4->base_0_19 | (pml4->base_20_39 << 20)) << 12); /* Get base address of PDP */ 

    pdp3 = (pdp3_entry_t *)v + ((virt_addr >> 30) & 0x1ff);
    if( tlb_entry_valid(pdp3) ) {
      pde2_entry_t *pde2;
      v = p2k_code((pdp3->base_0_19 | (pdp3->base_20_39 << 20)) << 12); /* Get base address of PDT */

      pde2 = (pde2_entry_t *)v + ((virt_addr >> 21) & 0x1ff);
      if( tlb_entry_valid(pde2) ) {
        /* Uffff, let's extract the PTE. */
        pte_t *pte;
        v = p2k_code((pde2->base_0_19 | (pde2->base_20_39 << 20)) << 12); /* Get base address of PTE */
        pte = (pte_t *)v + ((virt_addr >> 12) & 0x1ff);

        if( tlb_entry_valid(pte) ) {
          idx = pte->base_0_19 | (pte->base_20_39 << 20);
        } 
      } 
    } 
  }

  return idx;
}
