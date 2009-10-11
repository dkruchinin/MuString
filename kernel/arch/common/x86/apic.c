#include <config.h>
#include <mm/page.h>
#include <mm/mem.h>
#include <mm/page_alloc.h>
#include <arch/msr.h>
#include <arch/apic.h>
#include <arch/asm.h>
#include <arch/acpi.h>
#include <mstring/smp.h>
#include <mstring/interrupt.h>
#include <mstring/kprintf.h>
#include <mstring/assert.h>
#include <mstring/panic.h>
#include <mstring/types.h>

volatile uintptr_t lapic_addr;
uint32_t PER_CPU_VAR(lapic_ids);

static INITCODE void install_apic_irqs(void);

static inline uint32_t apic_read(uint32_t offset)
{
  volatile uint32_t *ret;
  
  ret = (volatile uint32_t *)(lapic_addr + offset);
  return *(uint32_t *)ret;
}

static inline void apic_write(uint32_t offset, uint32_t val)
{
  volatile uint32_t value = val;
  *(volatile uint32_t *)(lapic_addr + offset) = value;
}

static inline void lapic_eoi(void)
{
  apic_write(APIC_EOIR, 0);
}

static INITCODE void setup_bsp_apic(void)
{
  uint32_t apic_id;
  uint32_t apic_version;

  apic_id = apic_read(APIC_ID);
  apic_version = apic_read(APIC_VERSION);  
  kprintf("LAPIC ID = %d, version = %d, lvts = %d\n", apic_id, apic_version & 0xff,
          (apic_version >> 16) & 0xff);
}

static INITCODE void calibrate_lapic_timer(void)
{
}

void apic_spurious_interrupt(void)
{
  kprintf("APIC spurious interrupt on CPU #%d\n", cpu_id());
}

static void setup_apic_ldr_flat(cpu_id_t cpuid)
{
  uint32_t logid;
  
  ASSERT(cpuid < 256);
  apic_write(APIC_DFR, APIC_DFR_FLAT);
  logid = apic_read(APIC_LDR) & ~APIC_LDR_MASK;
  apic_write(APIC_LDR, logid << APIC_LOGID_SHIFT);
}

void local_apic_clear(void)
{
  apic_write(APIC_LI0_VTE, APIC_LVT_MASKED);
  apic_write(APIC_LI1_VTE, APIC_LVT_MASKED);
  apic_write(APIC_TIMER_LVTE, APIC_LVT_MASKED);
  apic_write(APIC_THERMAL_LVTE, APIC_LVT_MASKED);
  apic_write(APIC_PC_LVTE, APIC_LVT_MASKED);
  apic_write(APIC_ERROR_LVTE, APIC_LVT_MASKED);
}

void local_apic_enable(void)
{
  uint64_t val;

  val = msr_read(MSR_APIC_BAR);
  val |= APIC_BAR_ENABLED;
  msr_write(MSR_APIC_BAR, val);
}

void local_apic_disable(void)
{
  uint64_t val;

  val = msr_read(MSR_APIC_BAR);
  val &= ~APIC_BAR_ENABLED;
  msr_write(MSR_APIC_BAR, val);
}

static void lapic_mask_irq(irq_t irq_num)
{
  uint32_t val;
  
  switch (irq_num) {
      case APIC_ERROR_VECTOR:
        val = apic_read(APIC_ERROR_LVTE);
        apic_write(APIC_ERROR_LVTE, val | APIC_LVT_MASKED);
        break;
      default:
        panic("Unknown vector %d\n", irq_num);
  }
}

static bool lapic_can_handle_irq(irq_t irq)
{
  return true;
}

static void lapic_mask_all(void)
{
  return;
}

static void lapic_unmask_all(void)
{
  return;
}

static void lapic_unmask_irq(irq_t irq_num)
{
  switch (irq_num) {
      case APIC_ERROR_VECTOR:
        apic_write(APIC_ERROR_LVTE, APIC_ERROR_VECTOR);
        break;
      default:
        panic("Unknown IRQ vector %d\n", irq_num);
  }
}

static void lapic_ack_irq(irq_t irq_num)
{
  lapic_eoi();
}

static void lapic_error_interrupt(void *unused)
{
  uint32_t err;

  apic_write(APIC_ESR, 0);
  err = apic_read(APIC_ESR);
  kprintf("Lapic error: %#x on CPU %d\n", err, cpu_id());
}

static struct irq_controller lapic_controller = {
  .name = "Local APIC",
  .can_handle_irq = lapic_can_handle_irq,
  .mask_all = lapic_mask_all,
  .unmask_all = lapic_unmask_all,
  .mask_irq = lapic_mask_irq,
  .unmask_irq = lapic_unmask_irq,  
  .ack_irq = lapic_ack_irq,
};

static struct irq_action lapic_error_irq = {
  .name = "Local APIC Error",
  .handler = lapic_error_interrupt,
};

INITCODE void local_apic_init(cpu_id_t cpuid)
{
  int i, j;
  uint32_t val;

  if (!cpuid) {
    irq_register_controller(&lapic_controller);
    setup_bsp_apic();
    install_apic_irqs();
  }

  local_apic_enable();
  local_apic_clear();  
  
  /* set the lowest possible task priority */  
  val = apic_read(APIC_TPR) & ~APIC_VECTOR_MASK;
  apic_write(APIC_TPR, val);

  /* Clear the error status register */
  apic_write(APIC_ESR, 0);
  apic_write(APIC_ESR, 0);

  setup_apic_ldr_flat(cpuid);
  for (i = APIC_NUM_ISRS - 1; i >= 0; i--) {
    val = apic_read(APIC_ISR_BASE + i * 16);
    
    for (j = 31; j >= 0; j--) {
      if (bit_test(&val, j)) {
        lapic_eoi();
      }
    }
  }

  val = apic_read(APIC_SIVR) & ~APIC_SVR_MASK;
  val &= ~APIC_VECTOR_MASK;
  val |= (APIC_SPURIOUS_VECTOR | APIC_SVR_ENABLED | APIC_FP_DISABLED);
  apic_write(APIC_SIVR, val);

  /* setup error vector */
  apic_write(APIC_ERROR_LVTE, APIC_ERROR_VECTOR);
  
  if (!cpuid) {
    apic_write(APIC_LI0_VTE, APIC_LVTDM_EXTINT);
    apic_write(APIC_LI1_VTE, APIC_LVTDM_NMI);
  }
  else {
    apic_write(APIC_LI0_VTE, APIC_LVTDM_EXTINT | APIC_LVT_MASKED);
    apic_write(APIC_LI1_VTE, APIC_LVTDM_NMI | APIC_LVT_MASKED);
  }
}

INITCODE void local_apic_timer_init(void)
{
  //calibrate_lapic_timer();
}

static INITCODE void install_apic_irqs(void)
{
  int ret;
  irq_t irq;
  struct irq_action *act;

  irq = APIC_ERROR_VECTOR;
  ret = irq_line_register(irq, &lapic_controller);
  if (ret) {
    goto irqline_failed;
  }

  act = &lapic_error_irq;
  ret = irq_register_action(irq, act);
  if (ret) {
    goto irqaction_failed;
  }

  return;
irqline_failed:
  panic("Failed to register IRQ line #%d: [RET = %d]\n",
        irq, ret);
irqaction_failed:
  panic("Failed to register IRQ action %s: [RET = %d]\n",
        act->name, ret);
#if 0
#ifdef CONFIG_SMP
  idt->install_handler(smp_spurious_interrupt, APIC_SPURIOUS_VECTOR);
#endif /* CONFIG_SMP */
#endif
}

