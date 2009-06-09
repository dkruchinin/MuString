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
 * (c) Copyright 2008 Tirra <tirra.newly@gmail.com>
 * (c) Copyright 2008 Dmitry Gromada <gromada@jarios.org>
 *
 * mstring/amd64/smp.c: SMP support on amd64 architecture.
 *
 */

#include <config.h>
#include <mstring/interrupt.h>
#include <arch/8259.h>
#include <arch/i8254.h>
#include <arch/types.h>
#include <arch/asm.h>
#include <arch/apic.h>
#include <arch/interrupt.h>
#include <arch/gdt.h>
#include <mstring/kprintf.h>
#include <mstring/unistd.h>
#include <mstring/string.h>
#include <mstring/smp.h>
#include <arch/smp.h>

#ifdef CONFIG_SMP

void arch_smp_init(int ncpus)
{
  ptr_16_64_t gdtr;
  int i=1,r=0;

  /* ok setup new gdt */
  while(i<ncpus) {
    protected_ap_gdtr.limit=GDT_ITEMS * sizeof(struct __descriptor);
		memcpy(gdt[i], gdt[0], sizeof(descriptor_t) * GDT_ITEMS);
    protected_ap_gdtr.base=((uintptr_t)&gdt[i][0]-0xffffffff80000000);
    gdtr.base=(uint64_t)&gdt[i];
    
    if (apic_send_ipi_init(i))
			panic("Can't send init interrupt\n");
		
		/* wait maximum 1 second for the start of the next cpu */
		for (r = 0; (r < 100) && !is_cpu_online(i); r++)
			atom_usleep(10000);

		if (r == 100)
			panic("CPU %d did not start!\n", i);

    i++;
  }
}

void initialize_common_cpu_interrupts(void)
{
  
}

#endif /* CONFIG_SMP */
