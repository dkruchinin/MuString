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
 * eza/amd64/smp.c: SMP support on amd64 architecture.
 *
 */

#include <config.h>
#include <eza/interrupt.h>
#include <eza/arch/8259.h>
#include <eza/arch/i8254.h>
#include <eza/arch/types.h>
#include <eza/arch/asm.h>
#include <eza/arch/apic.h>
#include <eza/arch/ioapic.h>
#include <eza/arch/mm_types.h>
#include <eza/arch/interrupt.h>
#include <eza/arch/gdt.h>
#include <mlibc/kprintf.h>
#include <mlibc/unistd.h>
#include <mlibc/string.h>
#include <eza/arch/smp.h>

#ifdef CONFIG_SMP

/*
 * TODO: add acpi detection for smp
 *       it should follows from multiprocessor table
 */

void arch_smp_init(void)
{
  ptr_16_64_t gdtr;
  disable_all_irqs();

  /* set BIOS area to don't make a POST on INIT signal */
    /*  outb(0x70, 0xf); 
      outb(0x71, 0xa); */


  /* ok setup new gdt */
  protected_ap_gdtr.limit=GDT_ITEMS * sizeof(struct __descriptor);
  protected_ap_gdtr.base=((uintptr_t)&gdt[1][0]-0xffffffff80000000);
  gdtr.base=&gdt[1];

  apic_send_ipi_init(1);

  enable_all_irqs();
}

#endif /* CONFIG_SMP */
