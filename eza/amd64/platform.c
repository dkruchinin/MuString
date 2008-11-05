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
 * eza/amd64/platform.c: specific platform initialization.
 *
 */

#include <config.h>
#include <ds/iterator.h>
#include <eza/arch/types.h>
#include <eza/arch/apic.h>
#include <eza/arch/ioapic.h>
#include <eza/interrupt.h>
#include <eza/timer.h>
#include <mm/mm.h>
#include <mm/page.h>
#include <mm/pfalloc.h>
#include <mm/pt.h>
#include <mm/pfalloc.h>
#include <eza/arch/page.h>
#include <eza/arch/smp.h>
#include <mlibc/kprintf.h>
#include <mlibc/unistd.h>
#include <eza/swks.h>
#include <eza/arch/mm_types.h>
#include <mm/idalloc.h>
#include <config.h>

extern volatile struct __local_apic_t *local_apic;

static int __map_apic_page(void)
{
  page_frame_iterator_t pfi;
  ITERATOR_CTX(page_frame, PF_ITER_INDEX) pfi_index_ctx;
  int32_t res;
  uintptr_t apic_vaddr;

  apic_vaddr=(uintptr_t)idalloc_allocate_vregion(1);
  if( !apic_vaddr ) {
    panic( "[MM] Can't allocate memory range for mapping APIC !\n" );
  }

  mm_init_pfiter_index(&pfi, &pfi_index_ctx,
                       APIC_BASE >> PAGE_WIDTH,
                       APIC_BASE >> PAGE_WIDTH);

  res=__mm_map_pages(&pfi,apic_vaddr,1,
                     MAP_KERNEL | MAP_RW | MAP_DONTCACHE);

  if(res<0) {
    panic("[MM] Cannot map IO page for APIC.\n");
  }

  local_apic=(struct __local_apic_t *)apic_vaddr;
  return 0;
}

static int __map_ioapic_page(void)
{
  page_frame_iterator_t pfi;
  ITERATOR_CTX(page_frame, PF_ITER_INDEX) pfi_index_ctx;
  uint32_t res;

  mm_init_pfiter_index(&pfi, &pfi_index_ctx,
                       IOAPIC_BASE >> PAGE_WIDTH,
                       IOAPIC_BASE >> PAGE_WIDTH);

  res=__mm_map_pages(&pfi,IOAPIC_BASE,1,
                     MAP_KERNEL | MAP_RW | MAP_DONTCACHE);

  if(res<0) {
    kprintf("[MM] Cannot map IO page for IO APIC.\n");
    return -1;
  }

  return 0;
}

void arch_specific_init(void)
{
  int err=0;

  kprintf("[HW] Init arch specific ... ");

#ifndef CONFIG_APIC
  return;
#endif

  if(__map_apic_page()<0) 
    err++;
  if(__map_ioapic_page()<0) 
    err++;
  
  if(err) {
    kprintf("Fail\nErrors: %d\n",err);
    return;
  } else
    kprintf("OK\n");

  local_apic_bsp_switch(); 
  local_bsp_apic_init();

#ifdef CONFIG_SMP

  //arch_smp_init();
  //atom_usleep(50000);

#endif
  //local_apic_timer_init();
}

#ifdef CONFIG_SMP

void arch_ap_specific_init(void)
{
    local_ap_apic_init();
}

#endif

void arch_initialize_swks(void)
{
 /* According to the x86 programming manual, we must reserve
  * eight bits after the last available I/O port in all IOPMs.
  */
  swks.ioports_available=(PAGE_SIZE-sizeof(tss_t)-1)*8;
}
