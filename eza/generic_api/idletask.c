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

task_t *idle_tasks[NR_CPUS];

void clone_fn(void *data)
{
  int target_tick = swks.system_ticks_64 + 100;
  int rounds = 0;

  for( ;; ) {
    if( swks.system_ticks_64 == target_tick ) {
      kprintf( " + Tick, tick ! (Ticks: %d, PID: %d, CPU ID: %d)\n",
               swks.system_ticks_64, current_task()->pid, 1024 );
      target_tick += 10150;

      rounds ++;

      if( rounds >= 3 ) {
        task_t *t = idle_tasks[0];
        rounds = 0;

        arch_activate_task(t);
        target_tick = swks.system_ticks_64 + 100;
      }
    }
  }
}

task_t *kthread1 = NULL;

void idle_loop(void)
{
  int target_tick = swks.system_ticks_64 + 100;
  int rounds = 0;

  kernel_thread(clone_fn,NULL);

  /* TODO: [mt] Enable rescheduling interrupts here. */
  for( ;; ) {
    if( swks.system_ticks_64 == target_tick ) {
      kprintf( " - Tick, tick ! (Ticks: %d, PID: %d, CPU ID: %d)\n",
               swks.system_ticks_64, current_task()->pid, 1024 );
      target_tick += 10000;

      rounds++;

      if( rounds >= 3 ) {
        task_t *t = kthread1;

        rounds = 0;
        arch_activate_task(t);
        target_tick = swks.system_ticks_64 + 100;
      }
    }
  }
}

