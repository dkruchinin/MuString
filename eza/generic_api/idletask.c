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
#include <eza/spinlock.h>

task_t *idle_tasks[NR_CPUS];

#define STEP 1000

static void percpu_worker1(void *data)
{
  uint64_t target_tick = swks.system_ticks_64 + 100;

  kprintf( "+ [Worker N2] Greetings from my parent (%d): %s. My PID: %d\n",
           current_task()->ppid,data, current_task()->pid );

  for( ;; ) {
    if( swks.system_ticks_64 >= target_tick ) {
      kprintf( " + [%d] [Worker N2] Tuck, tuck ! CPU: %d (Ticks: %d, PID: %d, ID: %d, ATOM: %d)\n",
               cpu_id(), cpu_id(), swks.system_ticks_64, current_task()->pid,
               cpu_id(), in_atomic() );
      target_tick = swks.system_ticks_64 + STEP;
    }
  }
}

static void percpu_worker(void *data)
{
  uint64_t target_tick = swks.system_ticks_64 + 100;

  kprintf( "+ [Worker N1] Greetings from my parent (%d): %s. My PID: %d\n",
           current_task()->ppid,data, current_task()->pid );

  if( kernel_thread(percpu_worker1, "Run Second Worker, Run !!!") != 0 ) {
      panic( "Can't create per-cpu 2nd worker for CPU %d\n", cpu_id() );
  }

  for( ;; ) {
    if( swks.system_ticks_64 >= target_tick ) {
      kprintf( " + [%d] [Worker N1] Tuck, tuck ! CPU: %d (Ticks: %d, PID: %d, ID: %d, ATOM: %d)\n",
               cpu_id(), cpu_id(), swks.system_ticks_64, current_task()->pid,
               cpu_id(), in_atomic() );
      target_tick = swks.system_ticks_64 + STEP;
    }
  }
}

void idle_loop(void)
{
  uint64_t target_tick = swks.system_ticks_64 + 100;

  if( cpu_id() != 0 ) {
    if( kernel_thread(percpu_worker, "Run Worker, Run !!!") != 0 ) {
      panic( "Can't create per-cpu worker for CPU %d\n", cpu_id() );
    }
  }

  for( ;; ) {
    if( swks.system_ticks_64 >= target_tick ) {
      kprintf( " + [Idle N1] Tick, tick ! CPU: %d (Ticks: %d, PID: %d, ID: %d, ATOM: %d)\n",
               cpu_id(), swks.system_ticks_64, current_task()->pid, 1024, in_atomic() );
      target_tick += STEP;
    }
  }
}

