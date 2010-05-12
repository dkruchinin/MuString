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
 * (c) Copyright 2008,2009 <gromada@jarios.org>
 *
 * kernel/arch/common/x86/iopapic.c: implements IO APIC support driver.
 *
 */

#include <mstring/interrupt.h>
#include <mstring/kprintf.h>
#include <mstring/unistd.h>
#include <arch/types.h>
#include <arch/apic.h>
#include <arch/interrupt.h>
#include <arch/ioapic.h>

struct irq_pin {
  int napic;    /* apic chip number */
  int pin;
};

/* base virtual addresses where IO APIC areas are mapped */
static volatile uint32_t *ioapic_base_va[MAX_IOAPICS];
static irq_t ioapic_base_irqs[MAX_IOAPICS];
static int nr_ioapics;
static struct irq_pin irq_pin_map[NUM_IRQ_LINES];

/* basic functions for read/write from/to IO APIC */
static uint32_t ioapic_read(uint_t napic, uint8_t addr)
{
  ioapic_regsel_t rg;
  volatile uint32_t *base = ioapic_base_va[napic];

  rg.value = base[IOAPICRG]; /* save parameter and init selector*/
  rg.addr = addr; /* set address need to be ridden */
  base[IOAPICRG] = rg.value; /* restore set */

  return base[IOAPICWIN]; /* return data */
}

static void ioapic_write(uint_t napic, uint8_t addr, uint32_t v)
{
  ioapic_regsel_t rg;
  volatile uint32_t *base = ioapic_base_va[napic];

  /* the logic the same like in read, but we're write our value */
  rg.value = base[IOAPICRG]; /* save parameter and init selector*/
  rg.addr = addr; /* set address need to be ridden */
  base[IOAPICRG] = rg.value; /* restore set */
  base[IOAPICWIN] = v;
}

/* mask interrupt on io apic, disable interrupt */
static void ioapic_mask_irq(irq_t irq)
{
  ioapic_redir_t rd;
  int napic = irq_pin_map[irq].napic;
  int pin = irq_pin_map[irq].pin;

  /* read low bytes from redirection table for this irq */
  rd.low = ioapic_read(napic, (uint8_t) (IOAPICRED + pin * 2));
  rd.mask = 0x1;
  /* write low bytes from redirection table for this irq */
  ioapic_write(napic, (uint8_t) (IOAPICRED + pin * 2), rd.low);
}

/* unmask interrupt */
static void ioapic_unmask_irq(irq_t irq)
{
  ioapic_redir_t rd;
  int napic = irq_pin_map[irq].napic;
  int pin = irq_pin_map[irq].pin;

  /* read low bytes from redirection table for this irq */
  rd.low = ioapic_read(napic, (uint8_t) (IOAPICRED + pin * 2));
  rd.mask = 0x0;
  /* write low bytes from redirection table for this irq */
  ioapic_write(napic, (uint8_t) (IOAPICRED + pin * 2), rd.low);
}

static void ioapic_mask_all(void)
{
  int i;

  for (i=0; i < NUM_IRQ_LINES; i++) {
    if (irq_pin_map[i].napic >= 0)
      ioapic_mask_irq(i);
  }
}

static void ioapic_unmask_all(void)
{
  int i;

  for (i = 0; i < NUM_IRQ_LINES; i++) {
    if (irq_pin_map[i].napic >= 0)
      ioapic_unmask_irq(i);
  }
}

static void ioapic_ack_irq(irq_t irq)
{
  lapic_eoi();
}

static void ioapic_set_affinity(irq_t irq, cpumask_t mask)
{
  ioapic_redir_t rd;
  int napic = irq_pin_map[irq].napic;
  int pin_num = irq_pin_map[irq].pin;

  /* read high bytes from redirection table for this virq */
  rd.high = ioapic_read(napic, (uint8_t) (IOAPICRED + pin_num * 2 +1));

  /*
   * Interrupts were delivered only to 8 first CPUs.
   * For more CPUs physical destination mode
   * it's needed - TODO(?).
   */
  rd.dest = mask; /* set destination */

  /* write high bytes from redirection table for this irq */
  ioapic_write(napic, (uint8_t) (IOAPICRED + pin_num * 2 + 1), rd.high);
}

static struct irq_controller ioapic_controller = {
  .name = "IO APIC",
  .mask_all = ioapic_mask_all,
  .unmask_all = ioapic_unmask_all,
  .mask_irq = ioapic_mask_irq,
  .unmask_irq = ioapic_unmask_irq,
  .ack_irq = ioapic_ack_irq,
  .set_affinity = ioapic_set_affinity
};

static void __set_default_redir(irq_t irq)
{
  ioapic_redir_t rd;
  int napic = irq_pin_map[irq].napic;
  int pin_num = irq_pin_map[irq].pin;

  /* read low bytes from redirection table for this irq */
  rd.low = ioapic_read(napic, (uint8_t) (IOAPICRED + pin_num * 2));
  /* read high bytes from redirection table for this virq */
  rd.high = ioapic_read(napic, (uint8_t) (IOAPICRED + pin_num * 2 + 1));

  rd.dest = 0x01; /* set destination */
  rd.dmod = DMODE_LOGIC; /* set destination mode as logic */
  rd.trig_mode = TRIG_EDGE; /* set trigger mode to edge mode */
  rd.polarity = LEVEL_ASSERT; /* set high polarity */
  rd.txmod = TXMODE_FIXED; /* set delivery mode (TX) */
  rd.vector = IRQ_NUM_TO_VECTOR(irq); /* set destination vector */
  rd.mask = 0x1;  /* masked by default */

  /* write low bytes from redirection table for this virq */
  ioapic_write(napic, (uint8_t) (IOAPICRED + pin_num * 2), rd.low);
  /* write high bytes from redirection table for this irq */
  ioapic_write(napic, (uint8_t) (IOAPICRED + pin_num * 2 + 1), rd.high);
}

static void __ioapic_init(int napic)
{
  int i;
  ioapic_version_t ioapic_ver;
  int nr_pins;
  irq_t base_irq = ioapic_base_irqs[napic];
  struct irq_pin *ipin = irq_pin_map + base_irq;
  ioapic_id_t id;

  /* read version info */
  ioapic_ver.value = ioapic_read(napic, IOAPIC_VERREG);
  /* because this register contains max entries minus 1 ! */
  nr_pins = ioapic_ver.redir_entries + 1;
  id.value = ioapic_read(napic, IOAPIC_IDREG);

  kprintf("[HW] Initializing IO APIC: version = %#x, id = %d, base irq = %d, "
          "number of pins = %d... ", ioapic_ver.version, id.id, base_irq, nr_pins);

  /* register all irq lines and set default redirection entries */
  for(i = 0; i < nr_pins; i++, ipin++) {
    /* don't register irq 0 assigned to PIT and acknowledged to i8259 */
    if (i + base_irq) {
      ipin->napic = napic;
      ipin->pin = i;
      __set_default_redir(base_irq + i);
      irq_line_register(base_irq + i, &ioapic_controller);
    }
  }

  kprintf("OK\n");
}

void ioapic_init(void)
{
  int i;

  kprintf("[HW] Registering IO APIC module... ");
  irq_register_controller(&ioapic_controller);
  kprintf("OK\n");
  memset(irq_pin_map, -1, sizeof(irq_pin_map));

  for (i = 0; i < nr_ioapics; i++)
    __ioapic_init(i);
}

void save_next_ioapic_info(uint32_t *base_va, irq_t base_irq)
{
  ioapic_base_va[nr_ioapics] = base_va;
  ioapic_base_irqs[nr_ioapics] = base_irq;
  nr_ioapics++;
}
