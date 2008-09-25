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

static int __map_apic_page(void)
{
  page_frame_iterator_t pfi;
  ITERATOR_CTX(page_frame, PF_ITER_INDEX) pfi_index_ctx;
  int32_t res;

  mm_init_pfiter_index(&pfi, &pfi_index_ctx,
                       APIC_BASE >> PAGE_WIDTH,
                       APIC_BASE >> PAGE_WIDTH);

  res=__mm_map_pages(&pfi,APIC_BASE,1,
                     MAP_KERNEL | MAP_RW | MAP_DONTCACHE);

  if(res<0) {
    kprintf("[MM] Cannot map IO page for APIC.\n");
    return -1;
  }

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
  if(__map_apic_page()<0) 
    err++;
  if(__map_ioapic_page()<0) 
    err++;
  
  if(err) {
    kprintf("Fail\nErrors: %d\n",err);
    return;
  } else
    kprintf("OK\n");

  //io_apic_bsp_init();

  local_bsp_apic_init();

  //local_apic_timer_init();

  local_apic_bsp_switch();

  //  local_apic_timer_init();

#ifdef CONFIG_SMP

  arch_smp_init();
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
