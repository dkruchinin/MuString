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
 * mstring/generic_api/idletask.c: generic idle tasks-related functions.
 */

#include <config.h>
#include <mstring/task.h>
#include <mstring/swks.h>
#include <mstring/smp.h>

#ifdef CONFIG_SMP
/* FIXME: I don't know a better place for this global vaiable,
 * so if u'll find one, feel free to move smp_hooks there.
 */
LIST_DEFINE(smp_hooks);
#endif /* CONFIG_SMP */

#ifdef CONFIG_TEST
#include <test.h>
#endif

task_t *idle_tasks[CONFIG_NRCPUS];
#define STEP 600
#define TICKS_TO_WAIT 300

void idle_loop(void)
{
  long idle_cycles=0;

#ifdef CONFIG_TEST
  if( !cpu_id() ) {
    run_tests();
  }
#endif

  for( ;; ) {
    idle_cycles++;
  }
}

#ifdef CONFIG_TRACE_SYSCALL_ACTIVITY

void trace_sysenter(long syscall)
{
  if( current_task()->pid == CONFIG_TRACE_SYSCALL_ACTIVITY_TARGET ) {
    kprintf_fault("[trace_sysenter] Task (%d/%d), S=%d\n",
                  current_task()->pid,current_task()->tid,
                  syscall);
  }
}

void trace_sysreturn(long syscall, long retcode)
{
  if( current_task()->pid == CONFIG_TRACE_SYSCALL_ACTIVITY_TARGET ) {
    kprintf_fault("[trace_sysreturn] Task (%d/%d): S=%d, R=%d\n",
                  current_task()->pid,current_task()->tid,
                  syscall,retcode);
  }
}

#endif
