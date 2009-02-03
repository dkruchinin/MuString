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
 * eza/generic_api/idletask.c: generic idle tasks-related functions.
 */

#include <eza/kernel.h>
#include <eza/task.h>
#include <eza/swks.h>
#include <eza/smp.h>
#include <eza/timer.h>

#ifdef CONFIG_TEST
#include <test.h>
#endif

task_t *idle_tasks[CONFIG_NRCPUS];
#define STEP 600
#define TICKS_TO_WAIT 300

ulong_t syscall_counter = 0;

#define NUM_TIMERS 5

ktimer_t timers[NUM_TIMERS];

static void __timer_test(void)
{
  int i;
  ulong_t tx;

  kprintf("Initializing timers ... ");
  for(i=0;i<NUM_TIMERS;i++) {
    timers[i].da.priority=i+10;

    if( i == 2 ) {
      tx=100+90;
    } else {
      tx=100+90*i;
    }

    init_timer(&timers[i],tx);
  }
  kprintf("Done.\n");

  for(i=0;i<NUM_TIMERS;i++) {
    kprintf("Adding timer %d\n",i);
    add_timer(&timers[i]);
  }
}

void timer_thread(void *d)
{
  __timer_test();
  kprintf("TOTAL DONE !!!\n");
  for(;;);
}

void idle_loop(void)
{
  uint64_t target_tick = swks.system_ticks_64 + 100;

#ifdef CONFIG_TEST
  if( !cpu_id() ) {
    run_tests();
  }
#endif

  if( !cpu_id() ) {
    __timer_test();
  }

  for( ;; ) {
#ifndef CONFIG_TEST
    if( swks.system_ticks_64 >= target_tick ) {
//      kprintf( " + [Idle #%d] Tick, tick ! (Ticks: %d, PID: %d, ATOM: %d)\n",
//               cpu_id(), swks.system_ticks_64, current_task()->pid, in_atomic() );
      target_tick += STEP;
    }
#endif

  }
}

