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
 *
 * eza/amd64/opapic.c: implements IO APIC support driver.
 *
 */

#include <eza/interrupt.h>
#include <eza/arch/8259.h>
#include <eza/arch/i8254.h>
#include <eza/arch/types.h>
#include <eza/arch/asm.h>
#include <eza/arch/apic.h>
#include <eza/arch/mm_types.h>
#include <eza/arch/interrupt.h>
#include <eza/arch/ioapic.h>
#include <mlibc/kprintf.h>
#include <mlibc/unistd.h>

/*
 * NOTE: there are too black magic,
 * my attemption was to make full support 
 * in one day
 * Also, I didn't define a structure of io apic
 * in case of it's operational specific
 */

volatile static uint32_t *io_apic=(uint32_t *)IOAPIC_BASE;

/* basic functions for read/write from/to IO APIC */
uint32_t io_apic_read(uint8_t addr)
{
  io_apic_regsel_t rg;

  rg.value=io_apic[IOAPICRG]; /* save parameter and init selector*/
  rg.addr=addr; /* set address need to be ridden */
  io_apic[IOAPICRG]=rg.value; /* restore set */

  return io_apic[IOAPICWIN]; /* return data */
}

void io_apic_write(uint8_t addr,uint32_t v)
{
  io_apic_regsel_t rg;

  /* the logic the same like in read, but we're write our value */
  rg.value=io_apic[IOAPICRG]; /* save parameter and init selector*/
  rg.addr=addr; /* set address need to be ridden */
  io_apic[IOAPICRG]=rg.value; /* restore set */
  io_apic[IOAPICWIN]=v;
}

/* set IO redirection table item */
/*
 * virq - virtual irq (pin) to use
 * dest - destination addres for interrupt
 * vector - interrupt vector for triggering
 * flags - can be set to low priority
 */
void io_apic_set_ioredir(uint8_t virq,uint8_t dest,uint8_t vector,int flags)
{
  io_apic_redir_t rd;
  int txm=TXMODE_FIXED;

  if(flags & LOW_PRIORITY)
    txm=TXMODE_LOWPRI;

  rd.low=io_apic_read((uint8_t) (IOAPICRED + virq*2)); /* read low bytes from redirection table for this virq */
  rd.high=io_apic_read((uint8_t) (IOAPICRED + virq*2 +1)); /* read high bytes from redirection table for this virq */

  rd.dest=dest; /* set destination */
  rd.dmod=DMODE_LOGIC; /* set destination mode as logic */
  rd.trig_mode=TRIG_EDGE; /* set trigger mode to edge mode */
  rd.polarity=0x0; /* set high polarity */
  rd.txmod=txm; /* set delivery mode (TX) */
  rd.vector=vector; /* set destination vector */

  io_apic_write((uint8_t) (IOAPICRED + virq*2 +1),rd.high); /* write high bytes from redirection table for this virq */
  io_apic_write((uint8_t) (IOAPICRED + virq*2),rd.low); /* write low bytes from redirection table for this virq */
}

/* mask interrupt on io apic, disable interrupt */
void io_apic_disable_irq(uint32_t virq)
{
  io_apic_redir_t rd;

  rd.low=io_apic_read((uint8_t) (IOAPICRED + virq*2)); /* read low bytes from redirection table for this virq */
  rd.mask=0x1;
  io_apic_write((uint8_t) (IOAPICRED + virq*2),rd.low); /* write low bytes from redirection table for this virq */
}

/* enable interrupt */
void io_apic_enable_irq(uint32_t virq)
{
  io_apic_redir_t rd;

  rd.low=io_apic_read((uint8_t) (IOAPICRED + virq*2)); /* read low bytes from redirection table for this virq */
  rd.mask=0x0;
  io_apic_write((uint8_t) (IOAPICRED + virq*2),rd.low); /* write low bytes from redirection table for this virq */
}

static void local_apic_send_eoi_s(uint32_t v)
{
  local_apic_send_eoi();
}

static bool io_apic_handles_irq(uint32_t irq)
{
  return ( irq < 16 );
}

static void io_apic_disable_all(void)
{
  int i;

  for (i=0;i<16;i++)
    io_apic_disable_irq(i);
}

static void io_apic_enable_all(void)
{
  int i;

  for (i=0;i<16;i++)
    io_apic_enable_irq(i);
}

static hw_interrupt_controller_t io_apic_cont = {
  .descr = "IOAPIC",
  .handles_irq = io_apic_handles_irq,
  .enable_all = io_apic_disable_all,
  .disable_all = io_apic_enable_all,
  .enable_irq = io_apic_enable_irq,
  .disable_irq = io_apic_disable_irq,
  .ack_irq = local_apic_send_eoi_s,
};

uint32_t io_apic_pins;

void io_apic_bsp_init(void)
{
  int i;
  io_apic_id_t ioapic_id;
  io_apic_version_t ioapic_ver;

  /* read version info */
  ioapic_ver.value=io_apic_read(IOAPIC_VERREG);
  io_apic_pins=ioapic_ver.redir_entries+1; /* because this register contains max entries minus 1 ! */
  kprintf("[DBG] APIC pins = %d\n",io_apic_pins);

  ioapic_id.value=io_apic_read(IOAPIC_IDREG);
  ioapic_id.id=get_local_apic_id();

  io_apic_write(IOAPIC_IDREG,ioapic_id.value);

  /* ok, let's make a redir table */
    for(i=0;i<io_apic_pins;i++) {
      io_apic_set_ioredir(i,0x0,0x10+i,0x0);
    }
}

void io_apic_init(void)
{
  kprintf("[HW] IO APIC registering ... ");
  //register_hw_interrupt_controller( &io_apic_cont );
  kprintf("OK\n");
}
