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
#include <mstring/serial.h>
#include <arch/fault.h>

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

extern int irq_depth;
extern task_t *cpu0_current;

static char dbuf[512];
extern int irq_depth;
int timer0_depth;

long cur_syscall=0,log_step=0,left_syscall=0;
int tpid,ttid;

static void __cosher_serial_write(char *s)
{
  int i;

  for( i=0; s[i]; i++ ) {
    serial_write_char(s[i]);
  }
}

static void __show_cpu0_stats(void)
{
  int pid,tid;
  long urip;

  if( cpu0_current ) {
    pid=cpu0_current->pid;
    tid=cpu0_current->tid;
    urip=get_userspace_ip(cpu0_current);
  } else {
    pid=tid=-1;
    urip=0;
  }

  sprintf(dbuf,"[%d:%d] %p, d1:%d,d2:%d\n",
          pid,tid,urip,irq_depth,timer0_depth);
  __cosher_serial_write(dbuf);
}

void log_syscall_enter(long num)
{
  if( current_task()->pid == 15 ) {
    tpid=15;
    ttid=current_task()->tid;
    cur_syscall=num;
    log_step++;
  }
}

void log_syscall_return(long num)
{
  if( current_task()->pid == 15 ) {
    left_syscall=num;
    log_step++;
  }
}

static void second_idle(void)
{
  long prev_step=0;

  kprintf_fault("SECOND IDLE TASK is STARTING !\n");

  for(;;);

  while(1) {

    if( prev_step != log_step ) {
      prev_step=log_step;
      sprintf(dbuf,">[%d:%d] %d:%d\n",tpid,ttid,cur_syscall,left_syscall);
      __cosher_serial_write(dbuf);
    }
    //for( i=0; i < 500000000; i++ ) {
    //}

    //if( step & 0x1 ) {
    //  serial_write_char('.');
    //}
    //serial_write_char('0'+irq_depth);
    //serial_write_char('\n');

    //__show_cpu0_stats();
    //step++;
  }
}

ulong_t syscall_counter = 0;

void idle_loop(void)
{
  long idle_cycles=0;

  if( cpu_id() ) {
    second_idle();
  }

#ifdef CONFIG_TEST
  if( !cpu_id() ) {
    run_tests();
  }
#endif

  for( ;; ) {
    idle_cycles++;
  }
}

