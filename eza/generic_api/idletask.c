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
 * eza/generic_api/idletask.c: generic idle tasks-related functions.
 */

#include <eza/kernel.h>
#include <eza/task.h>
#include <eza/swks.h>
#include <eza/smp.h>
#include <eza/kcontrol.h>

#ifdef CONFIG_TEST
#include <test.h>
#endif

task_t *idle_tasks[CONFIG_NRCPUS];
#define STEP 600
#define TICKS_TO_WAIT 300

ulong_t syscall_counter = 0;

void sysctl_test(void)
{
  kcontrol_args_t ka;
  int n1[]={KCTRL_KERNEL,KCTRL_BOOT_INFO,KCTRL_INITRD_START_PAGE};
  int n2[]={KCTRL_KERNEL,KCTRL_BOOT_INFO,KCTRL_INITRD_SIZE};
  int d1,d2;
  int r;
  char *ramdisk;

  memset(&ka,0,sizeof(ka));
  ka.name=n1;
  ka.name_len=3;

  ka.old_data=&d1;
  ka.old_data_size=&d2;

  d1=d2=0;
  r=sys_kernel_control(&ka);
  kprintf("r=%d, d1=0x%X, d2=0x%X\n",
          r,d1,d2);

  ramdisk=pframe_id_to_virt(d1);
  kprintf("TAR MAGIC: %s\n",ramdisk+0x101);

  ka.name=n2;
  d1=d2=0;
  r=sys_kernel_control(&ka);
  kprintf("r=%d, d1=0x%X, d2=0x%X\n",
          r,d1,d2);

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
    sysctl_test();
  }

  for( ;; ) {
#ifndef CONFIG_TEST
    if( swks.system_ticks_64 >= target_tick ) {
        /*kprintf( " + [Idle #%d] Tick, tick ! (Ticks: %d, PID: %d, ATOM: %d)\n",
               cpu_id(), swks.system_ticks_64, current_task()->pid, in_atomic() );
               target_tick += STEP;*/
    }
#endif

  }
}

