#include <config.h>
#include <arch/boot.h>
#include <arch/seg.h>
#include <arch/msr.h>
#include <arch/fault.h>
#include <arch/smp.h>
#include <arch/mem.h>
#include <arch/current.h>
#include <arch/cpufeatures.h>
#include <mstring/smp.h>
#include <mstring/string.h>
#include <mstring/kconsole.h>
#include <mstring/kprintf.h>
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
            ((uint64_t)(GDT_SEL(KDATA_DESCR | SEG_DPL_USER)) << 48));
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

INITCODE void arch_cpu_init(cpu_id_t cpu)
{
  init_cpu_features();
  arch_seg_init(cpu);
  init_fs_and_gs(cpu);
  arch_idt_init();  
  syscall_init();
  if (cpu != 0) {
    arch_cpu_enable_paging();
  }

  syscall_init();
}

INITCODE void arch_init(void)
{
  kconsole_t *kcons = default_console();

  clear_bss();
  kcons->init();
  kcons->enable();

  multiboot_init();
  arch_cpu_init(0);
  install_fault_handlers();
}
