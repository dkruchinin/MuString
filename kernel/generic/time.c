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
 * (c) Copyright 2008 Michael Tsymbalyuk <mtzaurus@gmail.com>
 *
 * mstring/generic_api/time.c: contains implementation of the generic
 *                         functions that deal with time processing.
 *
 */

#include <mstring/time.h>
#include <mstring/types.h>
#include <mstring/swks.h>
#include <arch/timer.h>
#include <mstring/scheduler.h>
#include <mstring/smp.h>
#include <arch/current.h>
#include <arch/apic.h>
#include <mstring/timer.h>
#include <mstring/usercopy.h>
#include <kernel/syscalls.h>
#include <mstring/time.h>
#include <arch/interrupt.h>
#include <mstring/errno.h>
#include <mstring/signal.h>
#include <config.h>

#ifdef CONFIG_DEBUG_IRQ_ACTIVITY
#include <mstring/serial.h>
#endif

void initialize_timer(void)
{
  init_timers();
  arch_timer_init();
}

void timer_tick(void)
{
  /* Update the ticks counter. */
  swks.system_clock_ticks++;
  process_timers(); 
}

void setup_time(void)
{
  arch_setup_time();
}

void timer_interrupt_handler(void *data)
{
  timer_tick();
  sched_timer_tick();
}

int sys_nanosleep(timeval_t *in,timeval_t *out)
{
  timeval_t tv;
  ulong_t ticks;

  if( !in ) {
    return -EINVAL;
  }

  if( copy_from_user(&tv,in,sizeof(tv)) ) {
    return 0;
  }

  if( !tv.tv_sec && !tv.tv_nsec ) {
    return 0;
  }

  ticks=time_to_ticks(&tv);
  if( !ticks ) {
    /* TODO: [mt] support busy-wait cycle in 'sys_nanosleep()' */
    return 0;
  } else {
    sleep(ticks);
    return pending_signals_present(current_task()) ? -EINTR : 0;
  }
}

#ifdef CONFIG_SMP
/* SMP-specific stuff. */
void smp_local_timer_interrupt_tick(void)
{
#ifdef CONFIG_DEBUG_IRQ_ACTIVITY
  if( !(system_ticks & HZ) ) {
    serial_write_char('a'+cpu_id());
  }
#endif
    if(cpu_id() == 0) {
      timer_tick();
    }
    sched_timer_tick();
}
#endif
