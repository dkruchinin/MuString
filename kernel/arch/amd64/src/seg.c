#include <config.h>
#include <arch/seg.h>
#include <arch/asm.h>
#include <arch/page.h>
#include <mm/page.h>
#include <mstring/string.h>
#include <mstring/types.h>

segment_descr_t gdt[CONFIG_NRCPUS][GDT_ITEMS];
intr_descr_t idt[IDT_ITEMS];
tss_t tss[CONFIG_NRCPUS];

static char ist_stacks[TSS_USED_ISTS][IST_STACK_SIZE] __page_aligned__;

#define gdt_install_ldt(ldt_descr, dpl, base, limit, flags) \
  __install_tss(ldt_descr, SEG_TYPE_LDT, dpl, base, limit, flags)
#define gdt_install_tss(tss_descr, dpl, base, limit, flags) \
  __install_tss(tss_descr, SEG_TYPE_TSS, dpl, base, limit, flags)

static void __install_tss(struct tss_descr *descr, int type, uint8_t dpl,
                          uint64_t base, uint32_t limit, uint8_t flags)
{
  seg_descr_setup(&descr->seg_low, type, dpl,
                  base & 0xffffffffU, limit, flags);
  descr->seg_high.base_rest = (base >> 32) & 0xffffffffU;
  descr->seg_high.ignored = 0;
}

static void tss_init(tss_t *tssp)
{
  int i;
  
  memset(tssp, 0, sizeof(*tssp));
  tssp->ist1 = (uint64_t)&ist_stacks[0];
  /*for (i = 0; i < TSS_NUM_ISTS; i++) {
    if (i < TSS_USED_ISTS) {
      tssp->ists[i] = (uint64_t)&ist_stacks[i];
    }
    else {
      tssp->ists[i] = 0;
    }
    }*/
  
  tssp->iomap_base = TSS_BASIC_SIZE;
}

INITCODE void idt_install_gate(int slot, uint8_t type, uint8_t dpl,
                               uintptr_t handler, int ist)
{
  intr_descr_t *idt_descr;

  ASSERT((slot >= 0) && (slot < IDT_ITEMS));  
  idt_descr = &idt[slot];
  memset(idt_descr, 0, sizeof(*idt_descr));
  idt_descr->offset_low = handler & 0xffff;
  idt_descr->offset_midd = (handler >> 16) & 0xffff;
  idt_descr->offset_high = (handler >> 32) & 0xffffffffU;
  idt_descr->selector = GDT_SEL(KCODE_DESCR);
  idt_descr->ist = ist & 0x03;
  idt_descr->flags = type | ((dpl & 0x03) << SEG_DPL_SHIFT)
      | (SEG_FLG_PRESENT << 7);
}


INITCODE void arch_seg_init(cpu_id_t cpu)
{
  gdtr_t gdtr;

  memset(&gdt[cpu][0], 0, sizeof(gdt[cpu]));
  
  /* Null segment */
  seg_descr_setup(&gdt[cpu][NULL_DESCR], 0, 0, 0, 0, 0);

  /* Kernel code segment */
  seg_descr_setup(&gdt[cpu][KCODE_DESCR], SEG_TYPE_CODE, SEG_DPL_KERNEL,
                   0, 0xfffff, SEG_FLG_PRESENT | SEG_FLG_64BIT | SEG_FLG_GRAN);

  /* Kernel data segment */
  seg_descr_setup(&gdt[cpu][KDATA_DESCR], SEG_TYPE_DATA, SEG_DPL_KERNEL,
                  0, 0xfffff, SEG_FLG_PRESENT | SEG_FLG_64BIT | SEG_FLG_GRAN);

  /* User code segment */
  seg_descr_setup(&gdt[cpu][UCODE_DESCR], SEG_TYPE_CODE, SEG_DPL_USER,
                  0, 0xfffff, SEG_FLG_PRESENT | SEG_FLG_64BIT | SEG_FLG_GRAN);

  /* User data segment */
  seg_descr_setup(&gdt[cpu][UDATA_DESCR], SEG_TYPE_DATA, SEG_DPL_USER,
                  0, 0xfffff, SEG_FLG_PRESENT | SEG_FLG_64BIT | SEG_FLG_GRAN);

  /* Kerne 32bit code segment */
  seg_descr_setup(&gdt[cpu][KCODE32_DESCR], SEG_TYPE_CODE, SEG_DPL_KERNEL,
                  0, 0xfffff, SEG_FLG_PRESENT | SEG_FLG_OPSIZE | SEG_FLG_GRAN);
  
  /* TSS initialization */
  tss_init(&tss[cpu]);
  gdt_install_tss((tss_descr_t *)&gdt[cpu][TSS_DESCR], SEG_DPL_KERNEL,
                  (uint64_t)&tss[cpu], TSS_DEFAULT_LIMIT, SEG_FLG_PRESENT);

  gdtr.limit = sizeof(gdt) / CONFIG_NRCPUS;
  gdtr.base = (uint64_t)&gdt[cpu][0];

  gdtr_load(&gdtr);
  tr_load(GDT_SEL(TSS_DESCR));
}

INITCODE void arch_idt_init(void)
{
  idtr_t idtr;
  
  idtr.limit = sizeof(idt);
  idtr.base = (uint64_t)idt;
  idtr_load(&idtr);
}

 void seg_descr_setup(segment_descr_t *seg_descr, uint8_t type,
                     uint8_t dpl, uint32_t base,
                     uint32_t limit, uint8_t flags)
{
  seg_descr_set_limit(seg_descr, limit);
  seg_descr_set_base(seg_descr, base);

  /* set type and privilege level */
  seg_descr->flags_low = type | ((dpl & 0x03) << SEG_DPL_SHIFT);
  /* then set other flags */
  seg_descr->flags_low |= !!(flags & SEG_FLG_PRESENT) << 7;
  seg_descr->flags_high = (flags >> 1) & 0xf;
}

void load_ldt(cpu_id_t cpuid, void *ldt, uint32_t limit)
{
  gdt_install_ldt((ldt_descr_t *)&gdt[cpuid][LDT_DESCR], SEG_DPL_KERNEL,
                  (uintptr_t)ldt, limit, SEG_FLG_PRESENT);
  ldtr_load(GDT_SEL(LDT_DESCR));
}

void load_tss(cpu_id_t cpuid, void *tss, uint32_t limit)
{
  tss_descr_t *tssd;
  
  gdt_install_tss((tss_descr_t *)&gdt[cpuid][TSS_DESCR], SEG_DPL_KERNEL,
                  (uintptr_t)tss, limit, SEG_FLG_PRESENT);
  tr_load(GDT_SEL(TSS_DESCR));
}

void copy_tss(tss_t *dst_tss, tss_t *src_tss)
{
  memset(dst_tss, 0, sizeof(tss_t));
  dst_tss->rsp0 = src_tss->rsp0;

  /* Copy stack slot for doublefault handler. */
  //dst_tss->ists[0] = src_tss->ists[0];
  dst_tss->ist1 = src_tss->ist1;
}
