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
 * (c) Copyright 2006,2007,2008 MString Core Team <http://mstring.jarios.org>
 * (c) Copyright 2008 Tirra <tirra.newly@gmail.com>
 *
 * eza/amd64/i8254.c: implements i8254 timer driver.
 *
 */

#include <eza/interrupt.h>
#include <eza/timer.h>
#include <eza/arch/types.h>
#include <eza/arch/asm.h>
#include <eza/arch/i8254.h>
#include <mlibc/kprintf.h>
#include <mlibc/unistd.h>

hw_timer_t i8254;

/*low level functions*/
static void __arch_i8254_init(void)
{
  outb(I8254_BASE+3,0x36);
  disable_hw_irq(0x0);
  outb(I8254_BASE,(DCLOCK/CLOCK_HZ) & 0xf);
  outb(I8254_BASE,(DCLOCK/CLOCK_HZ) >> 8);
  enable_hw_irq(0x0);
}

uint64_t i8254_calibrate_delay_loop(void)
{
  uint8_t cnt;
  uint32_t tt1,tt2,oo1,oo2;

  outb(I8254_BASE+3,0x30);
  outb(I8254_BASE,0xff);
  outb(I8254_BASE,0xff);

  do {
    outb(I8254_BASE+3,0xc2);
    cnt=(uint8_t)((inb(I8254_BASE) >> 6) & 1);
    tt1=inb(I8254_BASE);
    tt1|=inb(I8254_BASE) << 8;
  } while(cnt);
  
  arch_delay_loop(DCLOCK);

  outb(I8254_BASE+3,0xd2);
  tt2=inb(I8254_BASE);
  tt2|=inb(I8254_BASE) << 8;

  outb(I8254_BASE+3,0xd2);
  oo1=inb(I8254_BASE);
  oo1|=inb(I8254_BASE) << 8;

  arch_fake_loop(DCLOCK);

  outb(I8254_BASE+3,0xd2);
  oo2=inb(I8254_BASE);
  oo2|=inb(I8254_BASE) << 8;

  __arch_i8254_init();

  return (((MAGIC_CLOCKN*DCLOCK)/1000) / ((tt1-tt2)-(oo1-oo2)))+
    (((MAGIC_CLOCKN*DCLOCK)/1000) % ((tt1-tt2)-(oo1-oo2)) ? 1 : 0);
}

uint64_t i8254_calibrate_delay_loop0(void)
{
  uint8_t cnt;
  uint32_t tt1,tt2,oo1,oo2;

  outb(I8254_BASE+3,0x30);
  outb(I8254_BASE,0xff);
  outb(I8254_BASE,0xff);

  do {
    outb(I8254_BASE+3,0xc2);
    cnt=(uint8_t)((inb(I8254_BASE) >> 6) & 1);
    tt1=inb(I8254_BASE);
    tt1|=inb(I8254_BASE) << 8;
  } while(cnt);
  
  atom_usleep(DCLOCK);

  outb(I8254_BASE+3,0xd2);
  tt2=inb(I8254_BASE);
  tt2|=inb(I8254_BASE) << 8;

  outb(I8254_BASE+3,0xd2);
  oo1=inb(I8254_BASE);
  oo1|=inb(I8254_BASE) << 8;

  arch_fake_loop(DCLOCK);

  outb(I8254_BASE+3,0xd2);
  oo2=inb(I8254_BASE);
  oo2|=inb(I8254_BASE) << 8;

  return (((MAGIC_CLOCKN*DCLOCK)/1000) / ((tt1-tt2)-(oo1-oo2)))+
    (((MAGIC_CLOCKN*DCLOCK)/1000) % ((tt1-tt2)-(oo1-oo2)) ? 1 : 0);
}

static void i8254_register_callback(irq_t irq,irq_handler_t handler)
{
  register_irq(irq,handler,NULL,0);
}

static void i8254_calibrate(uint32_t v)
{
  disable_hw_irq(0x0);
  outb(I8254_BASE,(DCLOCK/v) & 0xf);
  outb(I8254_BASE,(DCLOCK/v) >> 8);
  enable_hw_irq(0x0);
}

/*static*/ void i8254_suspend(void)
{
  outb(I8254_BASE+3,0x30);
  outb(I8254_BASE,0xff);
  outb(I8254_BASE,0xff);
}

static void i8254_resume(void)
{
  outb(I8254_BASE+3,0x36);
}

void i8254_init(void) 
{
  __arch_i8254_init();
  i8254.descr="generic";
  i8254.calibrate=i8254_calibrate;
  i8254.resume=i8254_resume;
  i8254.suspend=i8254_suspend;
  i8254.register_callback=i8254_register_callback;
  hw_timer_register(&i8254);
}
