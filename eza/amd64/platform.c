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
#include <eza/arch/types.h>
#include <eza/arch/apic.h>
#include <eza/arch/ioapic.h>
#include <eza/interrupt.h>
#include <eza/timer.h>
#include <mm/mm.h>
#include <mm/pt.h>
#include <eza/arch/page.h>
#include <eza/pageaccs.h>
#ifdef CONFIG_SMP
#include <eza/arch/smp.h>
#endif
#include <mlibc/kprintf.h>
#include <mlibc/unistd.h>

static int __map_apic_page(void)
{
  pageaccs_linear_pa_ctx_t pa_ctx;
  uint32_t res;

  pa_ctx.start_page=APIC_BASE >> PAGE_WIDTH;
  pa_ctx.end_page=APIC_BASE >> PAGE_WIDTH;

  pageaccs_linear_pa.reset(&pa_ctx);

  res=__mm_map_pages(&pageaccs_linear_pa,APIC_BASE,1,PF_IO_PAGE,&pa_ctx);

  if(res<0) {
    kprintf("[MM] Cannot map IO page for APIC.\n");
    return -1;
  }

  return 0;
}

static int __map_ioapic_page(void)
{
  pageaccs_linear_pa_ctx_t pa_ctx;
  uint32_t res;

  pa_ctx.start_page=IOAPIC_BASE >> PAGE_WIDTH;
  pa_ctx.end_page=IOAPIC_BASE >> PAGE_WIDTH;

  pageaccs_linear_pa.reset(&pa_ctx);

  res=__mm_map_pages(&pageaccs_linear_pa,IOAPIC_BASE,1,PF_IO_PAGE,&pa_ctx);

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
  //usleep(50000);

#endif
  //local_apic_timer_init();
}

#ifdef CONFIG_SMP

void arch_ap_specific_init(void)
{
  local_ap_apic_init();
}

#endif
