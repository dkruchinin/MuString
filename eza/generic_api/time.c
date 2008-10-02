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
 * (c) Copyright 2008 Michael Tsymbalyuk <mtzaurus@gmail.com>
 *
 * eza/generic_api/time.c: contains implementation of the generic
 *                         functions that deal with time processing.
 *
 */

#include <eza/time.h>
#include <eza/arch/types.h>
#include <eza/swks.h>
#include <eza/arch/timer.h>
#include <eza/scheduler.h>
#include <eza/smp.h>
#include <eza/arch/current.h>
#include <eza/arch/apic.h>

void initialize_timer(void)
{
  arch_timer_init();
}

static void process_timers(void)
{
}

void timer_tick(void)
{
  /* Update the ticks counter. */
  swks.system_ticks_64++;
  process_timers(); 
}

void timer_interrupt_handler(void *data)
{
  timer_tick();
}


#ifdef CONFIG_SMP
/* SMP-specific stuff. */
void smp_local_timer_interrupt_tick(void)
{
    if(cpu_id() == 0) {
        timer_tick();
    }
  sched_timer_tick();
}
#endif
