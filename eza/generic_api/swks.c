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
 * eza/generic_api/swks.c: Contains functions for dealing with "SWKS" -
 *                         "System-Wide Kernel Statistics", a global structure
 *                         that contains all kernel statistics visible to
 *                         all userspace applications.
 *
 */

#include <eza/arch/types.h>
#include <eza/arch/interrupt.h>
#include <eza/arch/timer.h>
#include <eza/swks.h>
#include <mlibc/kprintf.h>
#include <mlibc/string.h>
#include <eza/smp.h>

/* Here it is ! */
swks_t swks;

void initialize_swks(void)
{
  int i;
  char *p = (char *)&swks;
  for (i = 0; i < sizeof(swks); i++) {
    *p++ = 0;
  }
  //memset(&swks,0,sizeof(swks));

  swks.system_ticks_64 = INITIAL_TICKS_VALUE;
  swks.nr_cpus = NR_CPUS;
  swks.timer_frequency = HZ;
  kprintf("[LW] Calibrating delay loop ...");
  swks.delay_loop=arch_calibrate_delay_loop();
  kprintf("%ld.\n",swks.delay_loop);
}

