#include <arch/seg.h>
#include <arch/fault.h>
#include <arch/msr.h>
#include <server.h>
#include <arch/cpufeatures.h>
#include <arch/boot.h>
#include <arch/acpi.h>
#include <arch/apic.h>
#include <mstring/kprintf.h>
#include <mstring/smp.h>
#include <mstring/types.h>

uint32_t multiboot_info_ptr __attribute__ ((section(".data")));
uint32_t multiboot_magic __attribute__ ((section(".data")));
multiboot_info_t *mb_info;

int __bss_start, __bss_end, _kernel_end,
  __bootstrap_start, __bootstrap_end;

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
  msr_write(MSR_KERN_GS_BASE, (uintptr_t)raw_percpu_get_var(cpu_sched_stat, cpuid));
  msr_write(MSR_FS_BASE, 0);
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
  val &= ~CR0_AM;
  write_cr0(val);

  val = rflags_read();
  val &= ~((RFLAGS_IOPL_MASK << RFLAGS_IOPL_SHIFT) | RFLAGS_NT);
  rflags_write(val);

  val = read_cr4();
  val |= CR4_OSFXSR | CR4_OSXMMEXCPT;
  write_cr4(val);
}

static INITCODE void prepare_lapic_info(void)
{
  acpi_madt_t *madt;
  madt_lapic_t *lapic_tlb;
  uintptr_t dest_addr;
  int ret, i, cpu;
  uint32_t *lapic_id;

  madt = acpi_find_table(MADT_TABLE);
  if (likely(madt != NULL)) {
    lapic_addr = (uintptr_t)madt->lapic_addr;
  }
  else
  {
    kprintf(KO_WARNING "Can not get lapic address from ACPI!\n");
    lapic_addr = DEFAULT_LAPIC_ADDR;
  }

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
  if (ret) {
    panic("Failed to map local APIC address %p to address %p. [ERR = %d]",
          lapic_addr, dest_addr, ret);
  }

  lapic_addr = dest_addr;
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

INITCODE void arch_cpu_init(cpu_id_t cpu)
{
  init_cpu_features();
  arch_seg_init(cpu);
  arch_idt_init();
  init_fs_and_gs(cpu);
  syscall_init();
  if (cpu != 0) {
    arch_cpu_enable_paging();
  }

  syscall_init();
}

INITCODE void arch_prepare_system(void)
{
  kconsole_t *kcons = default_console();

  clear_bss();
  kcons->init();
  kcons->enable();

  multiboot_init();
  arch_cpu_init(0);
  arch_faults_init();
  arch_servers_init();  
}

INITCODE void arch_init(void)
{
  acpi_init();
  if (cpu_has_feature(X86_FTR_APIC)) {
    prepare_lapic_info();
  }
}
