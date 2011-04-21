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
 * mstring/amd64/smp.c: SMP support on amd64 architecture.
 *
 */

#include <config.h>
#include <arch/init.h>
#include <arch/asm.h>
#include <arch/apic.h>
#include <arch/seg.h>
#include <arch/smp.h>
#include <arch/cpufeatures.h>
#include <mstring/kprintf.h>
#include <mstring/interrupt.h>
#include <mstring/string.h>
#include <mstring/timer.h>
#include <mstring/smp.h>
#include <mstring/types.h>

#ifdef CONFIG_SMP
int ap_boot_start, ap_boot_end,
    kernel_jump_addr, ap_jmp_rip;

INITCODE void arch_smp_init(void)
{
  cpu_id_t c;
  uint32_t *lapic_id;
  char *ap_code = (char *)0x9000;
  size_t size = &ap_boot_end - &ap_boot_start;
  struct ap_config *apcfg;

  /* FIXME DK: real-mode page must be reserved dynamically during mm initialization. */
  apcfg = (struct ap_config *)&ap_config;
  apcfg->page_addr = 0x9000;
  apcfg->jmp_rip = (uint32_t)((uint64_t)smp_start32 & 0xffffffffU);
  apcfg->gdtr.base += apcfg->page_addr;
  memcpy(&apcfg->gdt[APGDT_KCOFF_DESCR], &apcfg->gdt[APGDT_KCNORM_DESCR],
         sizeof(uint64_t));
  apcfg->gdt[APGDT_KCOFF_DESCR] |= (uint64_t)apcfg->page_addr << 16;

  /* Change bootstrap entry point (from kernel_main to main_smpap_routine) */
  *(uint64_t *)&kernel_jump_addr = (uint64_t)main_smpap_routine;
  /* And finally copy AP initialization code to the proper place */
  memcpy(ap_code, &ap_boot_start, size);

  /* ok setup new gdt */
  for_each_cpu(c) {
      int r;
    if (!c) {
      continue; /* Skip BSP CPU */
    }

    lapic_id = raw_percpu_get_var(lapic_ids, c);
    kprintf("SEND INIT IPI TO CPU %d, cpuid=%d\n", *lapic_id, c);
    if (lapic_init_ipi(*lapic_id)) {
        panic("Can't send init interrupt\n");
    }
    interrupts_disable();
    for (r = 0; r < 100; r++) {
        default_hwclock->delay(1000);
        if (is_cpu_online(c)) {
            kprintf("online!\n");
            break;
        }
    }

    interrupts_enable();
    if (!is_cpu_online(c)) {
        kprintf("fuck! => %d\n", is_cpu_online(c));
      for (;;);
      panic("CPU %d is not online\n", c);
    }
  }
}

INITCODE void arch_processor_init(cpu_id_t cpuid)
{
  arch_cpu_init(cpuid);
  if (cpu_has_feature(X86_FTR_APIC)) {
    lapic_init(cpuid);
    lapic_timer_init(cpuid);
  }
}

void initialize_common_cpu_interrupts(void)
{
  
}

#endif /* CONFIG_SMP */
