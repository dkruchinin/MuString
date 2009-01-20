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
#include <ds/waitqueue.h>
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
#include <ipc/ipc.h>
#include <ipc/port.h>
#include <eza/arch/asm.h>
#include <eza/arch/preempt.h>
#include <kernel/syscalls.h>
#include <eza/uinterrupt.h>
#include <ipc/poll.h>
#include <eza/gc.h>
#include <ipc/gen_port.h>
#include <ipc/channel.h>
#include <eza/def_actions.h>

#ifdef CONFIG_TEST
#include <test.h>
#endif


task_t *idle_tasks[CONFIG_NRCPUS];
#define STEP 600
#define TICKS_TO_WAIT 300

ulong_t syscall_counter = 0;

task_t *server_task;
status_t server_port,server_port2,server_port3;

deffered_irq_action_t da1,da2,da3,da4,da5;
task_t t1,t2,t3;

static void test_def_actions(void)
{
  kprintf("Testing deffered actions.\n");

  t1.static_priority=10;
  t2.static_priority=3;
  t3.static_priority=89;

  DEFFERED_ACTION_INIT(da1,DEF_ACTION_EVENT,__DEF_ACT_SINGLETON_MASK);
  da1.target=&t1;

  DEFFERED_ACTION_INIT(da2,DEF_ACTION_EVENT,__DEF_ACT_SINGLETON_MASK);
  da2.target=&t1;

  DEFFERED_ACTION_INIT(da3,DEF_ACTION_EVENT,__DEF_ACT_SINGLETON_MASK);
  da3.target=&t2;


  DEFFERED_ACTION_INIT(da4,DEF_ACTION_EVENT,__DEF_ACT_SINGLETON_MASK);
  da4.target=&t3;

  DEFFERED_ACTION_INIT(da5,DEF_ACTION_EVENT,__DEF_ACT_SINGLETON_MASK);
  da5.target=&t3;

  schedule_deffered_action(&da1);
  schedule_deffered_action(&da2);
  schedule_deffered_action(&da3);
  schedule_deffered_action(&da4);
  schedule_deffered_action(&da5);

  kprintf( "+ Firing actions...\n" );
  fire_deffered_actions();
  kprintf("Finished testing deffered actions.\n");
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

//  if( !cpu_id() ) {
//    test_def_actions();
//  }

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

