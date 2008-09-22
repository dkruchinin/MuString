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
 * eza/generic_api/init.c: contains implementation of the 'init' startup logic.
 *
 */

#include <eza/kernel.h>
#include <mlibc/kprintf.h>
#include <eza/smp.h>
#include <eza/arch/scheduler.h>
#include <eza/arch/types.h>
#include <eza/task.h>
#include <eza/scheduler.h>
#include <eza/swks.h>
#include <mlibc/string.h>
#include <eza/arch/mm_types.h>
#include <eza/arch/preempt.h>


static void second_service_thread(void *data)
{
  uint64_t target_tick = swks.system_ticks_64 + 100;

  kprintf( "+ [Second service] Greetings from my parent (%d): %s\n",
           current_task()->ppid,data );

  for( ;; ) {
      if( swks.system_ticks_64 >= target_tick ) {
          target_tick = swks.system_ticks_64;
          kprintf( " + [Second service] Tick, tick ! (Ticks: %d, PID: %d, CPU: %d, ATOM: %d)\n",
               swks.system_ticks_64, current_task()->pid, 1024, in_atomic() );
          target_tick += 200;
    }
  }
}


static void init_thread(void *data)
{
  uint64_t target_tick = swks.system_ticks_64 + 100;
  status_t r;
  ulong_t max_timeslice = 400;

  r = sys_scheduler_control(1,SYS_SCHED_CTL_GET_POLICY,0);
  kprintf( "POLICY: %d\n", r );
  r = sys_scheduler_control(1,SYS_SCHED_CTL_GET_PRIORITY,0);
  kprintf( "PRIORITY: %d\n", r );
  r = sys_scheduler_control(1,SYS_SCHED_CTL_GET_AFFINITY_MASK,0);
  kprintf( "CPU AFFINITY: %d\n", r );

  kprintf( "INSUFFICIENT TASK: %d\n", sys_scheduler_control(4,SYS_SCHED_CTL_GET_MAX_TIMISLICE,0) );
  kprintf( "SETTING MAX TIMESLICE TO %d : result = %d\n", max_timeslice, sys_scheduler_control(1,SYS_SCHED_CTL_SET_MAX_TIMISLICE,max_timeslice) );

  r = sys_scheduler_control(1,SYS_SCHED_CTL_GET_MAX_TIMISLICE,0);
  kprintf( "MAX TIMESLICE: %d\n", r );

  if( kernel_thread(second_service_thread,"Run Service, Run !") != 0 ) {
    panic( "Can't create the Init task !" );
  }

  kprintf( "+ [Init] Greetings from my parent (%d): %s\n",
           current_task()->ppid,data );

  for( ;; ) {
    if( swks.system_ticks_64 >= target_tick ) {
        kprintf( " + [Init] Tick, tick ! (Ticks: %d, PID: %d, CPU: %d, ATOM: %d, T: %d)\n",
                 swks.system_ticks_64, current_task()->pid, 1024, in_atomic(), target_tick );
        target_tick = swks.system_ticks_64 + 200;
    }
  }
}

void start_init(void)
{
  if( kernel_thread(init_thread,"Run Init, Run !") != 0 ) {
    panic( "Can't create the Init task !" );
  }
}

