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
 * (c) Copyright 2008 Tirra <tirra.newly@gmail.com>
 *
 * include/eza/amd64/page.h: functions and definions for working with paging
 *                           on amd64
 *
 */

#ifndef __AMD64_PAGE_H__
#define __AMD64_PAGE_H__

/* Paging on amd64 is a real big fucking deal
 * by default you have mapping - one for kernel space,
 * other for user space.
 * According to the some 'stolen' documentation - 
 * Space range 0xffff800000000000 - 0xffffffff80000000 is 
 * a range for kernel space and it mapped to the physical 
 * memory one-in-one!
 * Space range 0xffffffff80000000 - 0xffffffffffffffff mapped
 * for user space...
 * Hacks are welcome now ... 
 */

/* ok, like usually we're like 4k page?! ;) */

#define PAGE_BOUND  12
#define PAGE_WIDTH  PAGE_BOUND
#define PAGE_SIZE   (1 << PAGE_WIDTH)
#define PAGE_ADDR_MASK (~(PAGE_SIZE-1))

#define KERNEL_BASE       0xffffffff80000000
#define KERNEL_OFFSET     0xffff800000000000
#define MAX_VIRT_ADDRESS  0xffffffffffffffff

#define KERNEL_INVALID_ADDRESS  0x100  /* Address that is never mapped. */

#define KERNEL_PHYS_START  0x100000
#define MIN_PHYS_MEMORY_REQUIRED 0x1800000

#ifndef __ASM__

#include <eza/arch/types.h>

static inline uintptr_t _k2p(uintptr_t p)
{
  if(p>KERNEL_BASE)
    return p-KERNEL_BASE;
  else
    return p-KERNEL_OFFSET;
}

#define k2p(p)       _k2p((uintptr_t) p)
#define p2k_code(p)  (((uintptr_t) (p))+KERNEL_BASE)
#define p2k(p)       (((uintptr_t) (p))+KERNEL_OFFSET)
#else
#define k2p(p)  ((p)-KERNEL_BASE)
#define p2k(p)  ((p)+KERNEL_BASE)
#endif /* __ASM__ */

/* ok, let's deal with PTL tables*/
/* constants for ptl flags*/
#define PTL_NO_EXEC        (1<<63)
#define PTL_ACCESSED       (1<<5)
#define PTL_CACHE_DISABLE  (1<<4)
#define PTL_CACHE_THROUGH  (1<<3)
#define PTL_USER           (1<<2)
#define PTL_WRITABLE       (1<<1)
#define PTL_PRESENT        1
#define PTL_2MB_PAGE       (1<<7)
/* firstly - 512 entries for each PTL level*/
#define _PTL_ENTRIES       512
#define PTL0_ENTRIES_ARCH  _PTL_ENTRIES
#define PTL1_ENTRIES_ARCH  _PTL_ENTRIES
#define PTL2_ENTRIES_ARCH  _PTL_ENTRIES
#define PTL3_ENTRIES_ARCH  _PTL_ENTRIES
/* size of table is logically == PAGE_SIZE */
#define PTL0_SIZE_ARCH  PAGE_SIZE
#define PTL1_SIZE_ARCH  PAGE_SIZE
#define PTL2_SIZE_ARCH  PAGE_SIZE
#define PTL3_SIZE_ARCH  PAGE_SIZE
/* other thing is an index calculating,it's simple */
#define ptl0_index_arch(vp)  (((vp) >> 39) & 0x1ff)
#define ptl1_index_arch(vp)  (((vp) >> 30) & 0x1ff)
#define ptl2_index_arch(vp)  (((vp) >> 21) & 0x1ff)
#define ptl3_index_arch(vp)  (((vp) >> 12) & 0x1ff)
/* for get access address of each PTE level we're using
 * the same macros, but it's good to separate it via naming
 */
#define get_ptl1_address_arch(ptl0,i) ((pte_t *) ((((uint64_t) ((pte_t *) (ptl0))[(i)].addr_12_31) << 12) | \
	     (((uint64_t) ((pte_t *) (ptl0))[(i)].addr_32_51) << 32)))
#define get_ptl2_address_arch(ptl1,i) ((pte_t *) ((((uint64_t) ((pte_t *) (ptl1))[(i)].addr_12_31) << 12) | \
	     (((uint64_t) ((pte_t *) (ptl1))[(i)].addr_32_51) << 32)))
#define get_ptl3_address_arch(ptl2,i) ((pte_t *) ((((uint64_t) ((pte_t *) (ptl2))[(i)].addr_12_31) << 12) | \
	     (((uint64_t) ((pte_t *) (ptl2))[(i)].addr_32_51) << 32)))
#define get_mmblock_address_arch(ptl3,i) ((uintptr_t *) \
				       ((((uint64_t) ((pte_t *) (ptl3))[(i)].addr_12_31) << 12) | \
					(((uint64_t) ((pte_t *) (ptl3))[(i)].addr_32_51) << 32)))
/* ok, now it's good to know how to set access address */
#define set_ptl0_address_arch(ptl0) (write_cr3((uintptr_t) (ptl0)))
#define set_ptl1_address_arch(ptl0,i,a) _set_pt_addr((pte_t*) (ptl0),(index_t) (i),a)
#define set_ptl2_address_arch(ptl1,i,a) _set_pt_addr((pte_t*) (ptl1),(index_t) (i),a)
#define set_ptl3_address_arch(ptl2,i,a) _set_pt_addr((pte_t*) (ptl2),(index_t) (i),a)
#define set_mmblock_address_arch(ptl3,i,a) _set_pt_addr((pte_t*) (ptl3),(index_t) (i),a)
/* now do the same with flags, get and set */
#define get_ptl1_flags_arch(ptl0,i) _get_pt_flags((pte_t*) (ptl0),(index_t) (i))
#define get_ptl2_flags_arch(ptl1,i) _get_pt_flags((pte_t*) (ptl1),(index_t) (i))
#define get_ptl3_flags_arch(ptl2,i) _get_pt_flags((pte_t*) (ptl2),(index_t) (i))
#define get_mmblock_flags_arch(ptl3,i) _get_pt_flags((pte_t*) (ptl3),(index_t) (i))
/* set flags */
#define set_ptl1_flags_arch(ptl0,i,f) _set_pt_flags((pte_t*) (ptl0),(index_t) (i),f) 
#define set_ptl2_flags_arch(ptl1,i,f) _set_pt_flags((pte_t*) (ptl1),(index_t) (i),f) 
#define set_ptl3_flags_arch(ptl2,i,f) _set_pt_flags((pte_t*) (ptl2),(index_t) (i),f) 
#define set_mmblock_flags_arch(ptl3,i,f) _set_pt_flags((pte_t*) (ptl3),(index_t) (i),f) 
/* test macros for working with pte */
#define PTE_VALID_ARCH(p)      (*((uint64_t *) p)!=0)
#define PTE_PRESENT_ARCH(p)    ((p)->present!=0)
#define PTE_WRITEABLE_ARCH(p)  ((p)->writeable!=0)
#define PTE_EXEC_ARCH(p)       ((p)->no_execute==0)
/* useful macros for getting address */
#define pte_get_mmblock_arch(p) ((((uintptr_t)(p)->addr_12_31) << 12) | (((uintptr_t)(p)->addr_32_51) << 32))

#ifndef __ASM__

/* error codes for page faults */
#define PGFLT_NS_PAGE  (1 << 0) /* no such page exist */
#define PGFLT_RO_PAGE  (1 << 1) /* write try page */
#define PGFLT_US_PAGE  (1 << 2) /* yep, user space burning - page fault from user space */
#define PGFLT_RS_PAGE  (1 << 3) /* reserved page access occured */ 
#define PGFLT_EX_PAGE  (1 << 4) /* execute page take a page fault */

/* inlined functions for playing with flags and address */
/*
static inline void _set_pt_addr(pte_t *p,index_t i,uintptr_t a)
{
  pte_t *__p=&p[i];

  __p->base_12_31=(a >> 12) & 0xfffff;
  __p->base_32_39=(a >> 32) & 0xff;

  return;
}

static inline int _get_pt_flags(pte_t *p,index_t i)
{
  pte_t *__p=&p[i];

  return ((!__p->page_cache_disable) << PAGE_CACHEABLE_SHIFT | (!__p->present) << PAGE_PRESENT_SHIFT |
	  __p->uaccessible << PAGE_USER_SHIFT | 1 << PAGE_READ_SHIFT | __p->writeable << PAGE_WRITE_SHIFT |
	  (!__p->no_execute) << PAGE_EXEC_SHIFT | __p->global << PAGE_GLOBAL_SHIFT);
}

static inline void _set_pt_flags(pte_t *p,index_t i,int f) 
{
  pte_t *__p=&p[i];

  __p->page_cache_disable=!(f & PAGE_CACHEABLE);
  __p->present=!(f & PAGE_NOT_PRESENT);
  __p->uaccessible=(f & PAGE_USER) != 0;
  __p->writeable=(f & PAGE_WRITE) != 0;
  __p->no_execute=(f & PAGE_EXEC) == 0;
  __p->global=(f & PAGE_GLOBAL) != 0;

  return;
}

*/

#endif /* __ASM__ */

/* varios stuff for paging and tss operating 
 * for more portability we assume that 
 * there are no segmantation in long mode
 */

#define IDT_ITEMS  256  /* interrupt descriptors */
#define GDT_ITEMS  9   /* GDT */

#define NIL_DES  0  /* nil(null) descriptor */
#define KTEXT_DES    1 /* kernel space */
#define KDATA_DES    2
#define UDATA_DES    3 /* user space */
#define UTEXT_DES    4
#define KTEXT32_DES  5 /* it's requered while bootstrap in 32bit mode */
#define TSS_DES      6

#define PL_KERNEL  0
#define PL_USER    3

#define AR_PRESENT    (1<<7)  /* avialable*/
#define AR_DATA       (2<<3)
#define AR_CODE       (3<<3)
#define AR_WRITEABLE  (1<<1)
#define AR_READABLE   (1<<1)
#define AR_TSS        (0x9)   /* task state segment */
#define AR_INTR       (0xe)   /* interrupt */
#define AR_TRAP       (0xf)

#define DPL_KERNEL  (PL_KERNEL<<5)
#define DPL_USPACE  (PL_USER<<5)

#define TSS_BASIC_SIZE  104
#define TSS_IOMAP_SIZE  ((4*4096)+1)  /* 16k&nil for mapping */

#define IO_PORTS        (IDT_ITEMS*1024)

/* bootstrap macros */
#define idtselector(des)  ((des) << 4)
#define gdtselector(des)  ((des) << 3)

#endif /* __AMD64_PAGE_H__ */

