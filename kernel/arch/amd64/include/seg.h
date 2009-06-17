/*
 * This program is free software ; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation ; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY ; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program ; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * (c) Copyright 2006,2007,2008 MString Core Team <http://mstring.jarios.org>
 * (c) Copyright 2009 Dan Kruchinin <dan.kruchinin@gmail.com>
 *
 */

#ifndef __MSTRING_ARCH_SEG_H__
#define __MSTRING_ARCH_SEG_H__

#include <config.h>

#define GDT_ITEMS 10
#define IDT_ITEMS 256

#define GDT_SEL(desc) ((desc) << 3)

#define NULL_DESCR    0
#define KCODE_DESCR   1
#define KDATA_DESCR   2
#define UDATA_DESCR   3
#define UCODE_DESCR   4
#define KCODE32_DESCR 5
#define TSS_DESCR     6
#define LDT_DESCR     8

#define LDT_ITEMS      2
#define SEG_DPL_SHIFT  5
#define SEG_DPL_KERNEL 0
#define SEG_DPL_USER   3

#define SEG_ATTR_A    0x01
#define SEG_ATTR_R    0x02
#define SEG_ATTR_W    0x02
#define SEG_ATTR_C    0x04
#define SEG_ATTR_E    0x04
#define SEG_ATTR_CODE 0x08
#define SEG_ATTR_DATA 0x00
#define SEG_ATTR_S    0x10
#define SEG_ATTR_P    0x80

#define SEG_FLG_PRESENT 0x01
#define SEG_FLG_AVAIL   0x02
#define SEG_FLG_64BIT   0x04
#define SEG_FLG_OPSIZE  0x08
#define SEG_FLG_GRAN    0x10

#define SEG_TYPE_CODE  (SEG_ATTR_C | SEG_ATTR_R | SEG_ATTR_CODE | SEG_ATTR_S)
#define SEG_TYPE_DATA  (SEG_ATTR_W | SEG_ATTR_DATA | SEG_ATTR_S)
#define SEG_TYPE_TSS   (SEG_ATTR_A | SEG_ATTR_CODE)
#define SEG_TYPE_LDT   (SEG_ATTR_W | SEG_ATTR_DATA)
#define SEG_TYPE_TRAP  (SEG_ATTR_A | SEG_ATTR_R | SEG_ATTR_C | SEG_ATTR_CODE)
#define SEG_TYPE_INTR  (SEG_ATTR_R | SEG_ATTR_C | SEG_ATTR_CODE)

#define PTD_SELECTOR 1

#ifndef __ASM__
#include <config.h>
#include <mstring/types.h>

typedef struct segment_descr {
  unsigned seg_limit_low     :16;
  unsigned base_address_low  :24;
  unsigned flags_low         :8;
  unsigned seg_limit_high    :4;
  unsigned flags_high        :4;
  unsigned base_address_high :8;
} __attribute__ ((packed)) segment_descr_t;

typedef struct intr_descr {
  unsigned offset_low  :16;
  unsigned selector    :16;
  unsigned ist         :3;
  unsigned ignored0    :5;
  unsigned flags       :8;
  unsigned offset_midd :16;
  unsigned offset_high :32;
  unsigned ignored1    :32;
} __attribute__ ((packed)) intr_descr_t;

#define TSS_NUM_ISTS      7
#define TSS_BASIC_SIZE    104
#define TSS_DEFAULT_LIMIT (TSS_BASIC_SIZE - 1)
#define TSS_IOPORTS_PAGES 2
/* Reserve last 8 bits. */
#define TSS_IOPORTS_LIMIT                                       \
  (TSS_IOPORTS_PAGES * PAGE_SIZE * 8 - 1 - TSS_BASIC_SIZE - 8)
#define IST_STACK_SIZE    PAGE_SIZE
#define TSS_USED_ISTS     1


typedef struct tss {
  uint32_t ignored0;
  uint64_t rsp0;
  uint64_t rsp1;
  uint64_t rsp2;
  uint64_t ignored1;
  uint64_t ist1;
  uint64_t ist2;
  uint64_t ist3;
  uint64_t ist4;
  uint64_t ist5;
  uint64_t ist6;
  uint64_t ist7;
  //uint64_t ists[TSS_NUM_ISTS];
  uint64_t ignored2;
  uint16_t ignored3;
  uint16_t iomap_base;
  uint8_t  iomap[];
} __attribute__ ((packed)) tss_t;

struct tss_descr {
  struct segment_descr seg_low;
  struct {
    uint32_t base_rest;
    uint32_t ignored;
  } seg_high __attribute__ ((packed));
} __attribute__ ((packed));

typedef struct table_reg gdtr_t;
typedef struct table_reg idtr_t;
typedef struct tss_descr tss_descr_t;
typedef struct tss_descr ldt_descr_t;

extern segment_descr_t gdt[CONFIG_NRCPUS][GDT_ITEMS];
extern intr_descr_t idt[IDT_ITEMS];
extern tss_t tss[CONFIG_NRCPUS];

INITCODE void arch_seg_init(cpu_id_t cpu);
INITCODE void arch_idt_init(void);
INITCODE void idt_install_gate(int slot, uint8_t type, uint8_t dpl,
                               uintptr_t handler, int ist);
void seg_descr_setup(segment_descr_t *seg_descr, uint8_t type,
                     uint8_t dpl, uint32_t base,
                     uint32_t limit, uint8_t flags);
void load_ldt(cpu_id_t cpuid, void *ldt, uint32_t limit);
void load_tss(cpu_id_t cpuid, void *tss, uint32_t limit);
void copy_tss(tss_t *dst_tss, tss_t *src_tss);

static inline tss_t *get_cpu_tss(cpu_id_t cpuid)
{
  return &tss[cpuid];
}

static inline void seg_descr_set_base(segment_descr_t *descr, uint32_t base)
{
  descr->base_address_low = base & 0xffffff;
  descr->base_address_high = (base >> 24) & 0xff;
}

static inline void seg_descr_set_limit(segment_descr_t *descr, uint32_t lim)
{
  descr->seg_limit_low = lim & 0xffff;
  descr->seg_limit_high = (lim >> 16) & 0xf;
}

#endif /* __ASM__ */
#endif /* __MSTRING_ARCH_SEG_H__ */
