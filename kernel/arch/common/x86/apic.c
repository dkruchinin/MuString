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
 * (c) Copytight 2009 Dan Kruchinin <dk@jarios.org>
 *
 */

#include <config.h>
#include <mm/page.h>
#include <mm/mem.h>
#include <mm/page_alloc.h>
#include <arch/msr.h>
#include <arch/apic.h>
#include <mstring/smp.h>
#include <mstring/interrupt.h>
#include <mstring/time.h>
#include <mstring/timer.h>
#include <mstring/kprintf.h>
#include <mstring/assert.h>
#include <mstring/panic.h>
#include <mstring/types.h>

volatile uintptr_t lapic_addr;
uint32_t PER_CPU_VAR(lapic_ids);

static INITCODE void install_lapic_irqs(void);

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

static int apic_get_maxlvt(void)
{
  uint_t v;

  v = apic_read(APIC_VERSION);
  return ((v >> 16) & 0xff);
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

#define APIC_CAL_LOOPS (HZ / 10)
#define APIC_DIVISOR   16

static tick_t lapic_timer_read(void)
{
  return apic_read(APIC_TIMER_CCR);
}

static void lapic_timer_delay(uint64_t nsec)
{
  panic("Unimpelemented!\n");
}

static struct hwclock lapic_timer = {
  .name = "Local APIC timer",
  .divisor = APIC_DIVISOR,
  .read = lapic_timer_read,
  .delay = lapic_timer_delay,
};

static void setup_lapic_ldr_flat(cpu_id_t cpuid)
{
  uint32_t logid;
  
  ASSERT(cpuid < 256);
  apic_write(APIC_DFR, APIC_DFR_FLAT);
  logid = apic_read(APIC_LDR) & ~APIC_LDR_MASK;
  logid  |= SET_APIC_LOGID(1UL << cpuid);
  apic_write(APIC_LDR, logid);
}

void local_apic_clear(void)
{
  uint_t val;
  int maxlvt;

  maxlvt = apic_get_maxlvt();
  if (maxlvt >= 3) {
    val = apic_read(APIC_ERROR_LVTE);
    apic_write(APIC_ERROR_LVTE, val | APIC_LVT_MASKED);
  }

  val = apic_read(APIC_LI0_VTE);
  apic_write(APIC_LI0_VTE, val | APIC_LVT_MASKED);
  val = apic_read(APIC_LI1_VTE);
  apic_write(APIC_LI1_VTE, val | APIC_LVT_MASKED);

  val = apic_read(APIC_TIMER_LVTE);
  apic_write(APIC_TIMER_LVTE, val | APIC_LVT_MASKED);

  if (maxlvt >= 5) {
    val = apic_read(APIC_THERMAL_LVTE);
    apic_write(APIC_THERMAL_LVTE, val | APIC_LVT_MASKED);
  }

  apic_write(APIC_PC_LVTE, APIC_LVT_MASKED);
  apic_write(APIC_TIMER_LVTE, APIC_LVT_MASKED);
  apic_write(APIC_LI0_VTE, APIC_LVT_MASKED);
  apic_write(APIC_LI1_VTE, APIC_LVT_MASKED);
  if (maxlvt >= 3) {
    apic_write(APIC_ERROR_LVTE, APIC_LVT_MASKED);
  }  
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
      case APIC_ERROR_IRQ:
        val = apic_read(APIC_ERROR_LVTE);
        apic_write(APIC_ERROR_LVTE, val | APIC_LVT_MASKED);
        break;
      case APIC_TIMER_IRQ:
        val = apic_read(APIC_TIMER_LVTE);
        apic_write(APIC_TIMER_LVTE, val | APIC_LVT_MASKED);
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
  uint_t val;

  switch (irq_num) {
      case APIC_ERROR_IRQ:
        apic_write(APIC_ERROR_LVTE, IRQ_NUM_TO_VECTOR(APIC_ERROR_IRQ));
        break;
      case APIC_TIMER_IRQ:
        apic_write(APIC_TIMER_LVTE, IRQ_NUM_TO_VECTOR(APIC_TIMER_IRQ));
        break;
      case APIC_SPURIOUS_IRQ:
        val = apic_read(APIC_SIVR);
        val |= IRQ_NUM_TO_VECTOR(APIC_SPURIOUS_IRQ);
        apic_write(APIC_SIVR, val);
        break;
      default:
        panic("Unknown IRQ vector %d\n", irq_num);
  }
}

static void lapic_ack_irq(irq_t irq_num)
{
  if (irq_num == APIC_TIMER_IRQ) {
    apic_write(APIC_TIMER_ICR, lapic_timer.freq / lapic_timer.divisor);
  }

  lapic_eoi();
}

static void lapic_spurious_handler(void *unused)
{
  kprintf("LAPIC SPURIOUS INTERRUPT\n");
}

static void lapic_error_interrupt(void *unused)
{
  uint32_t err, tmp;
  
  err = apic_read(APIC_ESR);
  apic_write(APIC_ESR, 0);
  tmp = apic_read(APIC_ESR);
  if (!(err & APIC_ERR_MASK)) {
    return;
  }
  kprintf(KO_ERROR "[LAPIC ERROR][CPU #%d] ", cpu_id());
  if (err & APIC_ERR_SAE) {
    kprintf("    Sent-accept error\n");
  }
  if (err & APIC_ERR_RAE) {
    kprintf("    Receive-accept error\n");
  }
  if (err & APIC_ERR_SIV) {
    kprintf("    Sent illegal vector\n");
  }
  if (err & APIC_ERR_RIV) {
    kprintf("    Receive illegal vector\n");
  }
  if (err & APIC_ERR_IRA) {
    kprintf("    Illegal register address\n");
  }
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

static struct irq_action lapic_spurious_irq = {
  .name = "Local APIC spurious IRQ",
  .handler = lapic_spurious_handler,
};

INITCODE void lapic_init(cpu_id_t cpuid)
{
  int i, j, maxlvt;
  uint32_t val;
  
  if (!cpuid) {
    kprintf("Going to setup lapic\n");
    irq_register_controller(&lapic_controller);
    setup_bsp_apic();
    kprintf("done");
  }

  local_apic_clear();
  local_apic_enable();  
  
  /* set the lowest possible task priority */  
  val = apic_read(APIC_TPR) & ~APIC_VECTOR_MASK;
  apic_write(APIC_TPR, val);

  /* Clear the error status register */
  apic_write(APIC_ESR, 0);
  apic_write(APIC_ESR, 0);

  setup_lapic_ldr_flat(cpuid);
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
  val |= (IRQ_NUM_TO_VECTOR(APIC_SPURIOUS_IRQ) | APIC_SVR_ENABLED);
  apic_write(APIC_SIVR, val);

  if (!cpuid) {
    apic_write(APIC_LI0_VTE, APIC_LVTDM_EXTINT);
    apic_write(APIC_LI1_VTE, APIC_LVTDM_NMI);
  }
  else {
    apic_write(APIC_LI0_VTE, APIC_LVTDM_EXTINT | APIC_LVT_MASKED);
    apic_write(APIC_LI1_VTE, APIC_LVTDM_NMI | APIC_LVT_MASKED);
  }

  lapic_eoi();
  maxlvt = apic_get_maxlvt();
  if (maxlvt > 3) {
    apic_write(APIC_ESR, 0);
  }
  
  apic_write(APIC_ERROR_LVTE, IRQ_NUM_TO_VECTOR(APIC_ERROR_IRQ));
  if (maxlvt > 3) {
    apic_write(APIC_ESR, 0);
  }

  val = apic_read(APIC_ESR);
  if (!cpuid) {
    install_lapic_irqs();
  }
}

static struct irq_action lapic_timer_irq = {
  .name = "Local APIC timer",
  .handler = timer_interrupt_handler,
};

static void lapic_icr_wait(void)
{
  int i;
  
  for (i = 0; i < 0x10000; i++) {
    if (!(apic_read(APIC_ICR_LOW) & APIC_LVTDS_SEND_PENDING)) {
      return;
    }
  }
}

INITCODE void lapic_timer_init(cpu_id_t cpuid)
{
  tick_t apictick0, apictick1, delta;
  uint32_t val;

  ASSERT(default_hwclock != NULL);
  if (!cpuid && irq_line_is_registered(PIT_IRQ)) {
    irq_mask(PIT_IRQ);
  }

  val = apic_read(APIC_TIMER_DCR);
  val &= ~(APIC_DV1 | APIC_TDR_DIV_TMBASE);
  val |= APIC_DV16;
  apic_write(APIC_TIMER_DCR, val);
  apic_write(APIC_TIMER_ICR, 0xffffffffU);

  //interrupts_save_and_disable(irqstat);
  apictick0 = apic_read(APIC_TIMER_CCR);
  default_hwclock->delay(APIC_CAL_LOOPS * 1000);
  apictick1 = apic_read(APIC_TIMER_CCR);
  //interrupts_restore(irqstat);

  delta = (apictick0 - apictick1) * APIC_DIVISOR / APIC_CAL_LOOPS;  
  val = APIC_LVT_TIMER_MODE; /* periodic timer mode */
  val |= APIC_LVT_MASKED;
  apic_write(APIC_TIMER_LVTE, val);

  apic_write(APIC_TIMER_ICR, delta / APIC_DIVISOR);  
  if (!cpuid) {
      lapic_timer.freq = delta;
      hwclock_register(&lapic_timer);
      ASSERT(irq_line_register(APIC_TIMER_IRQ, &lapic_controller) == 0);
      ASSERT(irq_register_action(APIC_TIMER_IRQ, &lapic_timer_irq) == 0);
  }
}

INITCODE int lapic_init_ipi(uint32_t apic_id)
{
  int i, maxlvts;

  maxlvts = apic_get_maxlvt();
  if (maxlvts > 3) {
    apic_write(APIC_ESR, 0);
  }

  apic_read(APIC_ESR);
  apic_write(APIC_ICR_HIGH, apic_id << APIC_LOGID_SHIFT);
  apic_write(APIC_ICR_LOW, APIC_LVTDM_INIT |
             APIC_LVT_TRIGGER_MODE | APIC_LVTL_ASSERT);
  lapic_icr_wait();

  /* wait for 10ms */
  default_hwclock->delay(10);

  apic_write(APIC_ICR_HIGH, apic_id << APIC_LOGID_SHIFT);
  apic_write(APIC_ICR_LOW, APIC_LVTDM_INIT |
             APIC_LVT_TRIGGER_MODE);
  lapic_icr_wait();
  if (apic_read(APIC_ICR_LOW) & APIC_LVTDS_SEND_PENDING) {
    return ERR(-1);
  }

  default_hwclock->delay(10);
  for (i = 0; i < 2; i++) {
    if (maxlvts > 3) {
      apic_write(APIC_ESR, 0);
    }

    apic_read(APIC_ESR);
    lapic_icr_wait();
    apic_write(APIC_ICR_HIGH, apic_id << APIC_LOGID_SHIFT);
    apic_write(APIC_ICR_LOW, APIC_LVTDM_STARTUP |
               (uint8_t)phys_to_pframe_id((void *)0x9000));
    if (maxlvts > 3) {
      apic_write(APIC_ESR, 0);
    }

    apic_read(APIC_ESR);
    default_hwclock->delay(300);
  }

  return 0;
}

static INITCODE void install_lapic_irqs(void)
{
  int ret;
  irq_t irq;
  struct irq_action *act;

  irq = APIC_ERROR_IRQ;
  ret = irq_line_register(irq, &lapic_controller);
  if (ret) {
    goto irqline_failed;
  }

  act = &lapic_error_irq;
  ret = irq_register_action(irq, act);
  if (ret) {
    goto irqaction_failed;
  }

  irq = APIC_SPURIOUS_IRQ;
  ret = irq_line_register(irq, &lapic_controller);
  if (ret) {
    goto irqline_failed;
  }
 
  act = &lapic_spurious_irq;
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

