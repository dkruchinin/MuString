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
 * (c) Copyright 2008 Tirra <madtirra@jarios.org>
 * (c) Copyright 2008 Dmitry Gromada <gromada@jarios.org>
 *
 * mstring/amd64/platform.c: specific platform initialization.
 *
 */

#include <config.h>
#include <arch/types.h>
#include <arch/apic.h>
#include <mstring/interrupt.h>
#include <mstring/timer.h>
#include <mm/page.h>
#include <mm/page_alloc.h>
#include <arch/page.h>
#include <arch/smp.h>
#include <mstring/kprintf.h>
#include <mstring/unistd.h>
#include <mstring/swks.h>
#include <mstring/actbl.h>

extern volatile uint32_t local_apic_base;
extern volatile uint8_t local_apic_ids[CONFIG_NRCPUS];

#ifdef CONFIG_APIC
void arch_specific_init(void)
{
  int r, i;
  uint32_t apic_base = 0;
  int n;
  uint8_t id;
	
  kprintf("[HW] Init arch specific ...\n");

  r = get_acpi_lapic_info(&apic_base, (uint8_t *)local_apic_ids, CONFIG_NRCPUS, &n);
  if (apic_base)
      local_apic_base = apic_base;
  
  local_apic_bsp_switch();
  if (local_bsp_apic_init() == -1)
      panic("Local APIC is not detected!\n");
  
#ifdef CONFIG_SMP
  if (r > 0) {
      /* ensure that the APIC ID of the bootstrap CPU is the first in the ID array */
      id = get_local_apic_id();
      for (i = 0; (i < r) && (local_apic_ids[i] != id); i++);

      if (i == r) {
          kprintf("Local apic information is invalid!\n");
          return;
      } else if (i) {
          local_apic_ids[i] = local_apic_ids[0];
          local_apic_ids[0] = id;
      }
		
      kprintf("[HW] Launch other cpus");
      if (r < n)
          kprintf(", found CPUs: %d, being launched CPUs: %d\n", n - 1, r - 1);
      else
          kprintf(", total number of CPUs: %d\n", r);

#if 0
      arch_smp_init(r);
#endif
  }
#endif
}

#else
void arch_specific_init(void)
{
	/* dummy */
}

#endif /* CONFIG_APIC */

#ifdef CONFIG_SMP

void arch_ap_specific_init(void)
{
	local_ap_apic_init();
}

#endif

static const int mon_days[12] = {
  31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

static ulong_t __init_tsepoch(void)
{
  ulong_t r=0; /* -1900 */
  int i;

  r=s_epoch.tm_sec + s_epoch.tm_min * 60 + s_epoch.tm_hour*3600 +
    (s_epoch.tm_mday-1)*86400;

  /* month */
  for(i=0;i<s_epoch.tm_mon;i++) {
    r+=mon_days[i]*86400;
    if((i==1) && (s_epoch.tm_year % 4 ==2 ))
      r+=86400;
  }

  /* year */
  for(i=0;i<s_epoch.tm_year;i++) {
    r+=365*86400;
    if(i % 4 == 2) r +=86400;
  }

  return r;
}

void arch_initialize_swks(void)
{
 /* According to the x86 programming manual, we must reserve
  * eight bits after the last available I/O port in all IOPMs.
  */
  swks.ioports_available=TSS_IOPORTS_LIMIT;
  swks.secs_since_epoch=__init_tsepoch();
}
