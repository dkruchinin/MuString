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
 * eza/amd64/apic.c: implements local APIC support driver.
 *
 */

#include <config.h>
#include <eza/interrupt.h>
#include <eza/arch/8259.h>
#include <eza/arch/i8254.h>
#include <eza/arch/types.h>
#include <eza/arch/asm.h>
#include <eza/arch/apic.h>
#include <eza/arch/mm_types.h>
#include <eza/arch/interrupt.h>
#include <mlibc/kprintf.h>
#include <mlibc/unistd.h>

/*
 * Black mages from intel and amd wrote that
 * local APIC is memory mapped, I'm afraid on this 
 * solution looks ugly ...
 * TODO: I get unclear sense while some higher 
 * abstraction not being implemented.
 */

volatile static struct __local_apic_t *local_apic=(struct __local_apic_t *)APIC_BASE;

/* 
 * default functions to access APIC (local APIC)
 * I think that gcc can try to make optimization on it
 * to avoid I'm stay `volatile` flag here.
 */
static inline uint32_t __apic_read(ulong_t rv)
{
  return *((volatile uint32_t *)(APIC_BASE+rv));
}

static inline void __apic_write(ulong_t rv,uint32_t val)
{
  *((volatile uint32_t *)(APIC_BASE+rv))=val;
}

static uint32_t __get_maxlvt(void)
{
  return local_apic->version.max_lvt;
}

static void __set_lvt_lint_vector(uint32_t lint_num,uint32_t vector)
{
  if(!lint_num)
    local_apic->lvt_lint0.vector=vector;
  else
    local_apic->lvt_lint1.vector=vector;
}

static void __enable_apic(void)
{
  local_apic->svr.apic_enabled |= (1 << 0);
  local_apic->svr.cpu_focus |= (1 << 0);
}

static void __disable_apic(void)
{
  local_apic->svr.apic_enabled &= ~(1 << 0);
  local_apic->svr.cpu_focus &= ~(1 << 0);
}

void __local_apic_clear(void)
{
  uint32_t max_lvt;
  uint32_t v;

  max_lvt=__get_maxlvt();

  if(max_lvt>=3) {
    v=0xfe;
    local_apic->lvt_error.vector=v;
    local_apic->lvt_error.mask |= (1 << 0);
  }

  /* mask timer and LVTs*/
  local_apic->lvt_timer.mask |= (1 << 0);
  local_apic->lvt_lint0.mask |= (1 << 0);
  local_apic->lvt_lint1.mask |= (1 << 0);

  if(max_lvt>=4) 
    local_apic->lvt_pc.mask |= (1 << 0);
  
}

static int __local_apic_check(void)
{
  uint32_t v0,v1;
  struct __local_apic_version_t *ver=(struct __local_apic_version_t *)&(local_apic->version);

  v0=local_apic->version.version;
  ver->version=0x0;
  v1=local_apic->version.version;
  if(v0!=v1) 
    return -1;

  /* version check */
  v0=(v1)&0xffu;
  if(v0==0x0 || v0==0xff)
    return -1;

  /*check for lvt*/
  v1=__get_maxlvt();
  if(v1<0x02 || v1==0xff)
    return -1;

  return 0;
}

void local_apic_send_eoi(void)
{
  local_apic->eoi.eoi=APIC_INT_EOI;
}

uint8_t get_local_apic_id(void)
{
  return local_apic->id.phy_apic_id;
}

/*init functions makes me happy*/
void local_bsp_apic_init(void)
{
  uint32_t v;

  kprintf("[LW] Checking APIC is present ... ");
  if(__local_apic_check()<0) {
    kprintf("FAIL\n");
    return;
  } else
    kprintf("OK\n");

  v=local_apic->version.version;
  kprintf("[LW] APIC version: %d\n",v);
  /* first we're need to clear APIC to avoid magical results */
  __local_apic_clear();

  /* enable APIC */
  __enable_apic();
  local_apic->svr.spurious_vector &= ~(1 << 8);
  /*set spurois vector*/
  local_apic->svr.spurious_vector |= (1 << 8);

  /* enable wire mode*/
  local_apic->lvt_lint0.mask &= ~(1 << 0);

  /* set nil vectors */

  __set_lvt_lint_vector(0,0x0);
  __set_lvt_lint_vector(1,0x0);


  /*set mode#7 for lint0*/
  local_apic->lvt_lint0.tx_mode=0x7;
  /*set mode#4 for lint1*/
  local_apic->lvt_lint1.tx_mode=0x4;

  local_apic->lvt_lint1.mask &= ~(1 << 0);

  /*tx_mode & polarity set to 0 on both lintx*/
  local_apic->lvt_lint0.tx_status &= ~(1 << 0);
  local_apic->lvt_lint0.polarity &= ~(1 << 0);
  local_apic->lvt_lint1.tx_status &= ~(1 << 0);
  local_apic->lvt_lint1.polarity &= ~(1 << 0);

  /* ok, now we're need to set esr vector to 0xfe */
  local_apic->lvt_error.vector = 0xfe;

  /*enable to receive errors*/
  if(__get_maxlvt()>3)
    local_apic->esr.tx_cs_err &= ~(1 << 0);


  /*FIXME: just for debug */


}

void local_apic_bsp_switch(void)
{
  kprintf("[LW] Leaving PIC mode to APIC mode ... ");
  outb(0x70,0x22);
  outb(0x71,0x23);

  kprintf("OK\n");
}

/* APIC timer implementation */
void local_apic_timer_enable(void)
{
  local_apic->lvt_timer.mask &= ~(1 << 0);
}

void local_apic_timer_disable(void)
{
  local_apic->lvt_timer.mask |= (1 << 0);
}

static void __local_apic_timer_calibrate(uint32_t x)
{
  switch(x) {
  case 1:    local_apic->timer_dcr.divisor=0xb;    break;
  case 2:    local_apic->timer_dcr.divisor=0x0;    break;
  case 4:    local_apic->timer_dcr.divisor=0x1;    break;
  case 8:    local_apic->timer_dcr.divisor=0x2;    break;
  case 16:    local_apic->timer_dcr.divisor=0x3;    break;
  case 32:    local_apic->timer_dcr.divisor=0x8;    break;
  case 64:    local_apic->timer_dcr.divisor=0x9;    break;
  case 128:    local_apic->timer_dcr.divisor=0xa;    break;
  default:    return;
  }
}

void local_apic_timer_calibrate(uint32_t hz)
{
  uint32_t x1,x2;

  x1=local_apic->timer_ccr.count; /*get current counter*/
  local_apic->timer_icr.count=fil; /*fillful initial counter */

  while(local_apic->timer_icr.count==x1) /* wait while cycle will be end */
    ;

  x1=local_apic->timer_ccr.count; /*get current counter*/
  usleep(1000000/hz); /*delay*/
  x2=local_apic->timer_ccr.count; /*again get current counter to see difference*/

  /*ok, let's write a difference to icr*/
  local_apic->timer_icr.count=x1-x2; /* <-- this will tell us how much ticks we're really need */
}

extern void i8254_suspend(void);

extern void timer_interrupt_handler(void *data);

void local_apic_timer_init(void)
{
  __local_apic_timer_calibrate(32);
  /* set periodic mode (set bit to 1) */
  local_apic->lvt_timer.timer_mode |= (1 << 0);
  /*calibrate to hz*/
  local_apic_timer_calibrate(HZ);
  /* enable timer */
  local_apic_timer_enable();
  //  i8254_suspend();
}

#ifdef CONFIG_SMP

void arch_smp_init(void)
{


}

#endif /* CONFIG_SMP */

