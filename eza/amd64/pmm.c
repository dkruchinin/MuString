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
 *     2008 SMP redesign by Michael Tsymbalyuk <mtzaurus@gmail.com> 
 *
 * eza/amd64/pmm.c: paging and segmentation initing
 *
 */

#include <mm/page.h>
#include <eza/arch/types.h>
#include <eza/arch/page.h>
#include <eza/arch/mm_types.h>
#include <mlibc/string.h>
#include <eza/arch/asm.h>
#include <eza/smp.h>
#include <eza/arch/gdt.h>
#include <eza/errno.h>
#include <eza/interrupt.h>
#include <mm/mm.h>
#include <eza/arch/mm.h>

/* Global per-CPU GDT entries. */
descriptor_t gdt[NR_CPUS][GDT_ITEMS]={
  GDT_CPU_ENTRIES,
  GDT_CPU_ENTRIES
};

/* Global per-cpu TSS entries. */
tss_t tss[NR_CPUS];

/* Global IDT array */
idescriptor_t idt[IDT_ITEMS];

/* init tss - just fill it nil */
void tss_init(tss_t *tp)
{
  memset((void*)tp,'\0',sizeof(tss_t));

  return;
}

void idt_init(void)
{
  return;
}

/* misc functions for gdt & idt */
void gdt_tss_setbase(descriptor_t *p, uintptr_t baddre)
{
  tss_descriptor_t *o=(tss_descriptor_t *)p;

  o->base_0_15=baddre & 0xffff;
  o->base_16_23=((baddre) >> 16) & 0xff;
  o->base_24_31=((baddre) >> 24) & 0xff;
  o->base_32_63=((baddre) >> 32);

  return;
}

void gdt_tss_setlim(descriptor_t *p, uint32_t lim)
{
  tss_descriptor_t *o=(tss_descriptor_t *)p;

  o->limit_0_15=lim & 0xffff;
  o->limit_16_19=(lim >> 16) & 0xf;

  return;
}

void idt_set_offset(idescriptor_t *p, uintptr_t off)                             
{
  p->offset_0_15=off & 0xffff;
  p->offset_16_31=off >> 16 & 0xffff;
  p->offset_32_63=off >> 32;

  return;
}

int install_trap_gate( uint32_t slot, uintptr_t handler,
                       prot_ring_t dpl, ist_stack_frame_t ist ) {
  if( slot < IDT_ITEMS && handler != 0  ) {
    idescriptor_t *p = &idt[slot];

    idt_set_offset(p,handler);
    p->selector = gdtselector(KTEXT_DES);
    p->unused = 0;
    p->type = AR_TRAP;
    p->present = 1;
    p->dpl = dpl;
    p->ist = ist;

    return 0;
  }
  return -EINVAL;
}


int install_interrupt_gate( uint32_t slot, uintptr_t handler,
                            prot_ring_t dpl, ist_stack_frame_t ist ) {
  if( slot < IDT_ITEMS ) {
    idescriptor_t *p = &idt[slot];

    idt_set_offset(p,handler);

    p->selector = gdtselector(KTEXT_DES); 
    p->type = AR_INTR;
    p->present = 1;
    p->dpl = dpl;
    p->ist = ist;

    return 0;
  }
  return -EINVAL;
}


void arch_pmm_init(cpu_id_t cpu)
{
  tss_descriptor_t *tss_dsc;
  ptr_16_64_t gdtr;
  tss_t *tss_p;
  ptr_16_64_t idtr;

  /* Only master CPU has to initialize the IDT. */
  if( cpu == 0 ) {
    idt_init();
  }

  /* Other descriptor tables are initialized in the same way
   * by all CPUs
   */
  tss_p=&tss[cpu];
  tss_init(tss_p);
  tss_dsc=(tss_descriptor_t*)&gdt[cpu][TSS_DES];
  tss_dsc->present=1;
  tss_dsc->type=AR_TSS;
  tss_dsc->dpl=PL_KERNEL;
  gdt_tss_setbase(&gdt[cpu][TSS_DES],(uintptr_t)tss_p);

  gdtr.limit = sizeof(gdt) / NR_CPUS;
  gdtr.base = (uint64_t)&gdt[cpu][0];
  gdt_tss_setlim(&gdt[cpu][TSS_DES],(uintptr_t)TSS_BASIC_SIZE-1);

  idtr.limit = sizeof(idt);
  idtr.base = (uint64_t)idt;

  /* Now load CPU registers with new descriptors. */
  gdtr_load(&gdtr);
  tr_load(gdtselector(TSS_DES));
  idtr_load(&idtr);

  /* Build page array. */
  if( cpu == 0 ) {
    build_page_array(); 
  }

  /* Remap memory using 4K page frames. All CPUs has to do this. */
  arch_remap_memory(cpu);

  return;
}

