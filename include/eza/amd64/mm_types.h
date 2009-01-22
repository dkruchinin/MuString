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
 * (c) Copyright 2008 Michael Tsymbalyuk <mtzaurus@gmail.com>
 *
 * include/eza/amd64/mm_types.h: types definions for gdt/idt operations
 *
 */

#ifndef __MM_TYPES_H__
#define __MM_TYPES_H__

#include <eza/arch/types.h>
#include <eza/arch/page.h>

typedef enum __prot_ring {
  PROT_RING_0 = 0,
  PROT_RING_1 = 1,
  PROT_RING_2 = 2,
  PROT_RING_3 = 3,
} prot_ring_t;

typedef enum __ist_stack_frame {
  IST_STACK_SLOT_1 = 1,
  IST_STACK_SLOT_2,
  IST_STACK_SLOT_3,
  IST_STACK_SLOT_4,
  IST_STACK_SLOT_5,
  IST_STACK_SLOT_6,
  IST_STACK_SLOT_7,
} ist_stack_frame_t;

struct __descriptor { /* global descriptor */
  unsigned limit_0_15: 16;
  unsigned base_0_15: 16;
  unsigned base_16_23: 8;
  unsigned access: 8;
  unsigned limit_16_19: 4;
  unsigned available: 1;
  unsigned longmode: 1;
  unsigned special: 1;
  unsigned granularity : 1;
  unsigned base_24_31: 8;
} __attribute__ ((packed));
typedef struct __descriptor descriptor_t;

struct __tss_descriptor { /* task state segments descriptor */
  unsigned limit_0_15: 16;
  unsigned base_0_15: 16;
  unsigned base_16_23: 8;
  unsigned type: 4;
  unsigned : 1;
  unsigned dpl : 2;
  unsigned present : 1;
  unsigned limit_16_19: 4;
  unsigned available: 1;
  unsigned : 2;
  unsigned granularity : 1;
  unsigned base_24_31: 8;
  unsigned base_32_63 : 32;
  unsigned  : 32;
} __attribute__ ((packed));
typedef struct __tss_descriptor tss_descriptor_t;

typedef struct __tss_descriptor ldt_descriptor_t;

struct __intrdescriptor { /* interrupt descriptor */
  unsigned offset_0_15: 16;
  unsigned selector: 16;
  unsigned ist: 3;
  unsigned unused: 5;
  unsigned type: 5;
  unsigned dpl: 2;
  unsigned present: 1;
  unsigned offset_16_31: 16;
  unsigned offset_32_63: 32;
  unsigned  : 32;
} __attribute__ ((packed));
typedef struct __intrdescriptor idescriptor_t;

struct __ptr_16_64 { /* area with 16bit size limitation */
  uint16_t limit;
  uint64_t base;
} __attribute__ ((packed));
typedef struct __ptr_16_64 ptr_16_64_t;
                                                                                   
struct __ptr_16_32 { /* area with 16bit size limitation (32bit compatibility) */
  uint16_t limit;
  uint32_t base;
} __attribute__ ((packed));
typedef struct __ptr_16_32 ptr_16_32_t;

struct __tss { /* TSS definion structure */
  uint32_t reserve1;
  uint64_t rsp0;
  uint64_t rsp1;
  uint64_t rsp2;
  uint64_t reserve2;
  uint64_t ist1;
  uint64_t ist2;
  uint64_t ist3;
  uint64_t ist4;
  uint64_t ist5;
  uint64_t ist6;
  uint64_t ist7;
  uint64_t reserve3;
  uint16_t reserve4;
  uint16_t iomap_base;
  uint8_t iomap[];
} __attribute__ ((packed));
typedef struct __tss tss_t;

/* external functions and global vars(pmm.c) */
extern descriptor_t gdt[CONFIG_NRCPUS][GDT_ITEMS];
extern idescriptor_t idt[];
extern ptr_16_64_t gdtr;
extern ptr_16_32_t boot_gdt; /* GDT during boot */
extern ptr_16_32_t prot_gdt; /* GDT while protected mode */
extern tss_t *tssp; /* TSS */

/* initing functions */
extern void arch_pmm_init(cpu_id_t cpu);
extern void idt_init(void);
extern void tss_init(tss_t *tp);

/* context switching - related functions. */
void load_tss(cpu_id_t cpu,tss_t *new_tss,uint16_t limit);
void load_ldt(cpu_id_t cpu,uintptr_t ldt,uint16_t limit);
void copy_tss(tss_t *dst_tss,tss_t *src_tss);

/* gdt related misc functions */
extern void gdt_tss_setbase(descriptor_t *p,uintptr_t baddre);
extern void gdt_tss_setlim(descriptor_t *p,uint32_t lim);
/* idt realted misc functions */
extern void idt_set_offset(idescriptor_t *p,uintptr_t off);

void descriptor_set_base(descriptor_t *d,uint32_t base);

extern tss_t *get_cpu_tss(cpu_id_t cpu);

/* Functions for dealing with traps and gates. */
int install_trap_gate( uint32_t slot, uintptr_t handler,
                       prot_ring_t dpl, ist_stack_frame_t ist );
int install_interrupt_gate( uint32_t slot, uintptr_t handler,
                            prot_ring_t dpl, ist_stack_frame_t ist );


#endif /* __MM_TYPES_H__ */

