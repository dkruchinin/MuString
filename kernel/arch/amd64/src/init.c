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
 * (c) Copyright 2009 Dan Kruchinin <dk@jarios.org>
 *
 */

#include <arch/seg.h>
#include <arch/fault.h>
#include <arch/msr.h>
#include <server.h>
#include <arch/cpufeatures.h>
#include <arch/boot.h>
#include <arch/acpi.h>
#include <arch/apic.h>
#include <arch/ioapic.h>
#include <arch/current.h>
#include <mstring/kprintf.h>
#include <mstring/smp.h>
#include <mstring/task.h>
#include <mstring/types.h>

uint32_t multiboot_info_ptr __attribute__ ((section(".data")));
uint32_t multiboot_magic __attribute__ ((section(".data")));
multiboot_info_t *mb_info;

int __bss_start, __bss_end, _kernel_end,
  __bootstrap_start, __bootstrap_end;

static INITDATA task_t dummy_task;

static void multiboot_init(void)
{
  if (multiboot_magic != MULTIBOOT_LOADER_MAGIC) {
    kprintf("FATAL ERROR: Invalid multiboot magic(%#x). %#x was expected\n",
            multiboot_magic, MULTIBOOT_LOADER_MAGIC);
    for (;;);
  }

  mb_info = (multiboot_info_t *)(uintptr_t)multiboot_info_ptr;
}

static INITCODE void init_fs_and_gs(cpu_id_t cpuid)
{
  msr_write(MSR_GS_BASE, 0);
  msr_write(MSR_KERN_GS_BASE,
            (uintptr_t)raw_percpu_get_var(cpu_sched_stat, cpuid));
  msr_write(MSR_FS_BASE, 0);
  __asm__ volatile ("swapgs");
}

/* Just clear BSS section */
static INITCODE void clear_bss(void)
{
  size_t bss_size = (ulong_t)&__bss_end - (ulong_t)&__bss_start;
  memset((void *)&__bss_start, 0, bss_size);
}

static INITCODE void syscall_init(void)
{
  if (!cpu_has_feature(AMD64_FTR_SYSCSR)) {
    panic("CPU doesn't support SYSCALL/SYSRET instructions!");
  }

  efer_set_feature(EFER_SCE);
  msr_write(MSR_STAR,
            ((uint64_t)(GDT_SEL(KCODE_DESCR) | SEG_DPL_KERNEL) << 32) |
            ((uint64_t)(GDT_SEL(KDATA_DESCR) | SEG_DPL_USER) << 48));
  msr_write(MSR_LSTAR, (uint64_t)syscall_point);
  msr_write(MSR_SF_MASK, 0x200);
}

static INITCODE void init_cpu_features(void)
{
  uint64_t val;

  val = read_cr0();
  val &= ~CR0_AM; /* Disable alignment-check */

  /*
   * Set write protect bit in order to protect
   * write access to read-only pages from supervisor mode.
   */
  val |= CR0_WP;
  write_cr0(val);

  val = rflags_read();
  val &= ~((RFLAGS_IOPL_MASK << RFLAGS_IOPL_SHIFT) | RFLAGS_NT);
  rflags_write(val);

  val = read_cr4();
  val |= CR4_OSFXSR | CR4_OSXMMEXCPT;
  write_cr4(val);
}

static INITCODE int map_lapic_page(void)
{
  uintptr_t dest_addr;
  int ret;

  ASSERT(lapic_addr != 0);
#ifdef CONFIG_AMD64
  dest_addr = __allocate_vregion(LAPIC_NUM_PAGES);
  if (!dest_addr) {
    panic("Failed to allocate virtual region for %d local APIC pages!",
          LAPIC_NUM_PAGES);
  }
#else /* CONFIG_AMD64 */
  dest_addr = DEFAULT_LAPIC_ADDR;
#endif /* !CONFIG_AMD64 */

  ret = mmap_page(KERNEL_ROOT_PDIR(), dest_addr,
                  phys_to_pframe_id((void *)lapic_addr),
                  KMAP_READ | KMAP_WRITE | KMAP_KERN | KMAP_NOCACHE);
  if (!ret) {
    lapic_addr = dest_addr;
  }

  return ret;
}

static INITCODE void prepare_lapic_info(void)
{
  acpi_madt_t *madt;
  madt_lapic_t *lapic_tlb;
  int ret, i, cpu;
  uint32_t *lapic_id;

  madt = acpi_find_table(MADT_TABLE);
  if (likely(madt != NULL)) {
    lapic_addr = (uintptr_t)madt->lapic_addr;
  }
  else {
    kprintf(KO_WARNING "Can not get lapic address from ACPI!\n");
    lapic_addr = DEFAULT_LAPIC_ADDR;
  }

  ret = map_lapic_page();
  if (ret) {
    panic("Failed to map local APIC page. [ERR = %d]", ret);
  }

  i = 1; cpu = 0;
  for_each_percpu_var(lapic_id, lapic_ids) {
    lapic_tlb = madt_find_table(madt, MADT_LAPIC, i);
    if (!lapic_tlb) {
      panic("Failed to get MADT_LAPIC table for cpu %d\n", cpu);
    }
    if (!lapic_tlb->flags.enabled) {
      panic("Lapic disabled on CPU %d\n", cpu);
    }

    *lapic_id = lapic_tlb->apic_id;
    i++;
    cpu++;
  }
}

static INITCODE void __prepare_one_ioapic(uintptr_t base_addr, irq_t base_irq)
{
  uintptr_t va;

#ifdef CONFIG_AMD64
  va = __allocate_vregion(IOAPIC_NUM_PAGES);
  if (!va) {
    panic("Failed to allocate virtual region for %d IO APIC pages!",
          IOAPIC_NUM_PAGES);
  }
#else /* CONFIG_AMD64 */
  va = base_addr;
#endif /* !CONFIG_AMD64 */

  if (mmap_kern(va, phys_to_pframe_id((void*)base_addr), IOAPIC_NUM_PAGES,
                KMAP_KERN | KMAP_READ | KMAP_WRITE | KMAP_NOCACHE | KMAP_REMAP)) {
    panic("Failed to map IO APIC space!");
  }

  save_next_ioapic_info((uint32_t*)va, base_irq);
}

static INITCODE void prepare_ioapic_info(void)
{
  acpi_madt_t *madt;
  madt_ioapic_t *ioapic_tbl;
  int i;
  /* default IOAPIC initialization */
  bool init_def = false;

  madt = acpi_find_table(MADT_TABLE);
  if (unlikely(madt == NULL)) {
    kprintf(KO_WARNING "Can not get the multiple APIC description table from ACPI!\n");
    init_def = true;
  } else {
    for (i = 0; i < MAX_IOAPICS; i++) {
      ioapic_tbl = madt_find_table(madt, MADT_IO_APIC, i + 1);
      if (ioapic_tbl == NULL) {
        if (!i) {
          kprintf(KO_WARNING "IO APIC records miss in the multiple APIC description table\n");
          init_def = true;
        }
        break;
      }

      __prepare_one_ioapic((uintptr_t)ioapic_tbl->base_addr, ioapic_tbl->start_gsi);
    }
  }

  if (init_def)
    __prepare_one_ioapic(DEFAULT_IOAPIC_ADDR, 0);
}

INITCODE void arch_cpu_init(cpu_id_t cpu)
{
  init_fs_and_gs(cpu);
  init_cpu_features();
  arch_seg_init(cpu);
  arch_idt_init();
  syscall_init();
  if (cpu != 0) {
    arch_cpu_enable_paging();
  }
}

INITCODE void arch_prepare_system(void)
{
  kconsole_t *kcons = default_console();
  cpu_sched_stat_t *sched_stat = raw_percpu_get_var(cpu_sched_stat, 0);

  clear_bss();
  sched_stat->current_task = &dummy_task;
  kcons->init();
  kcons->enable();

  kcons = fault_console();
  kcons->init();
  kcons->enable();

  multiboot_init();
  arch_cpu_init(0);
  arch_faults_init();
  arch_servers_init();
}

INITCODE void arch_init(void)
{
  bool cpu_has_acpi = true;

  if (acpi_init() < 0) {
    cpu_has_acpi = false;
    kprintf(KO_WARNING "Your BIOS doesn't provide ACPI!\n");
  }
  if (cpu_has_feature(X86_FTR_APIC)) {
    if (cpu_has_acpi) {
      prepare_lapic_info();
      prepare_ioapic_info();
    } else {
      cpu_id_t c;
      int ret;

      lapic_addr = DEFAULT_LAPIC_ADDR;
      for_each_cpu(c) {
        raw_percpu_set_var(lapic_ids, c, (uint32_t)c);
      }

      ret = map_lapic_page();
      if (ret) {
        panic("Failed to mmap local APIC page: [RET = %d]\n", ret);
      }

      __prepare_one_ioapic(DEFAULT_IOAPIC_ADDR, 0);
    }
  }
}
