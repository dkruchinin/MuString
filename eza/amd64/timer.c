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
 * (c) Copyright 2008 MadTirra <tirra.newly@gmail.com>
 *
 * eza/amd64/timer.c: arch specific timers init
 *                          
 *
 */

#include <eza/arch/types.h>
#include <eza/arch/i8254.h>
#include <eza/timer.h>
#include <eza/time.h>
#include <mlibc/kprintf.h>
#include <config.h>

extern void i8254_resume(void);
extern void i8254_suspend(void);

void arch_timer_init(void)
{
  int i;

  i8254_init();

  kprintf("[LW] Calibrating delay loop ... ");
  delay_loop=i8254_calibrate_delay_loop();

#ifdef CONFIG_APIC 
  for(i=0;i<10;i++) {
    delay_loop=i8254_calibrate_delay_loop0();
  }
#endif

  kprintf("%ld\n",delay_loop);
}

uint64_t arch_calibrate_delay_loop(void)
{
  return i8254_calibrate_delay_loop();
}
