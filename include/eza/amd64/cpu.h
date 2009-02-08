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
 * (c) Copyright 2005,2008 Tirra <tirra.newly@gmail.com>
 * (c) Copyright 2008 Dan Kruchinin <dan.kruchinin@gmail.com>
 *
 * include/eza/amd64/cpu.h: defines used for cpu and cpuid functionality, also
 *                          used in bootstrap and cpu identification
 *
 */

#ifndef __ARCH_CPU_H__
#define __ARCH_CPU_H__

/* specific amd flags and registers */
#define AMD_MSR_STAR    0xc0000081 /* msr */
#define AMD_MSR_LSTAR   0xc0000082
#define AMD_MSR_SFMASK  0xc0000084
#define AMD_MSR_FS      0xc0000100
#define AMD_MSR_GS      0xc0000101
#define AMD_MSR_GS_KRN  0xc0000102
#define EFER_MSR_NUM    0xc0000080
/* flags */
#define AMD_SCE_FLAG    0
#define AMD_LME_FLAG    8
#define AMD_LMA_FLAG    10
#define AMD_FFXSR_FLAG  14
#define AMD_NXE_FLAG    11

#define RFLAGS_IF  (1<<9)
#define RFLAGS_RF  (1<<16)

/* cpuid stuff */
/* feature flags */
#define AMD_CPUID_EXTENDED   0x80000001 /* amd64 */
#define AMD_EXT_NOEXECUTE    20
#define AMD_EXT_LONG_MODE    29
#define INTEL_CPUID_STANDARD 0x00000001 /* intel */
#define INTEL_CPUID_EXTENDED 0x80000000
#define INTEL_SSE2           26
#define INTEL_FXSAVE         24

#define IDT_ITEMS  256  /* interrupt descriptors */
#define GDT_ITEMS  8   /* GDT */

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
#define TSS_DEFAULT_LIMIT  (TSS_BASIC_SIZE-1)
#define TSS_IOPORTS_PAGES  1
#define TSS_IOPORTS_LIMIT  (PAGE_SIZE-1)

#define PTD_SELECTOR  1  /* LDT selector that refers to per-task data. */

#define IO_PORTS        (IDT_ITEMS*1024)
#define LDT_DES      8
#define LDT_ITEMS  2    /* Default number of LDT entries (must include 'nil') */
#define AR_LDT        (0x2)   /* 64-bit LDT descriptor */

/* bootstrap macros */
#define idtselector(des)  ((des) << 4)
#define gdtselector(des)  ((des) << 3)

/* Macros for defining task selectors. */
#define USER_SELECTOR(s) (gdtselector(s) | PL_USER)
#define KERNEL_SELECTOR(s) gdtselector(s)

#ifndef __ASM__

#include <config.h>
#include <eza/arch/page.h>
#include <eza/arch/types.h>

typedef uint32_t cpu_id_t;

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

#ifdef CONFIG_SMP
#define __percpu__ /*__attribute__((__section__(".percpu_data")))*/
#else
#define __percpu__
#endif /* CONFIG_SMP */

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
void copy_tss(tss_t *dst_tss,tss_t *src_tss);
void load_ldt(cpu_id_t cpu,uintptr_t ldt,uint16_t limit);

/* gdt related misc functions */
extern void gdt_tss_setbase(descriptor_t *p,uintptr_t baddre);
extern void gdt_tss_setlim(descriptor_t *p,uint32_t lim);
/* idt realted misc functions */
extern void idt_set_offset(idescriptor_t *p,uintptr_t off);

extern tss_t *get_cpu_tss(cpu_id_t cpu);

/* Functions for dealing with traps and gates. */
int install_trap_gate( uint32_t slot, uintptr_t handler,
                       prot_ring_t dpl, ist_stack_frame_t ist );
int install_interrupt_gate( uint32_t slot, uintptr_t handler,
                            prot_ring_t dpl, ist_stack_frame_t ist );
void descriptor_set_base(descriptor_t *d,uint32_t base);

/* varios CPU functions from cpu.c */
void arch_cpu_init(cpu_id_t cpu);

#endif /* !__ASM__ */

#endif /* __ARCH_CPU_H__ */

