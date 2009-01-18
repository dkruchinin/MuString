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

#include <mlibc/types.h>
#include <eza/arch/interrupt.h>
#include <eza/arch/timer.h>
#include <eza/swks.h>
#include <mlibc/kprintf.h>
#include <mlibc/string.h>
#include <eza/smp.h>
#include <eza/vm.h>
#include <kernel/vm.h>
#include <mm/mmap.h>

/* Here it is ! */
swks_t swks  __attribute__((aligned(PAGE_SIZE)));
static vm_range_t swks_area;

void initialize_swks(void)
{
  int i;
  char *p = (char *)&swks;
  for (i = 0; i < sizeof(swks); i++) {
    *p++ = 0;
  }

  swks.system_ticks_64 = INITIAL_TICKS_VALUE;
  swks.nr_cpus = CONFIG_NRCPUS;
  swks.timer_frequency = HZ;
  /*kprintf("[LW] Calibrating delay loop ...");
  swks.delay_loop=arch_calibrate_delay_loop();
  kprintf("%ld.\n",swks.delay_loop);*/

  arch_initialize_swks();

  /* Make the SWKS be mapped into every user address space. */
  swks_area.phys_addr=k2p(&swks) & PAGE_ADDR_MASK;
  swks_area.virt_addr=SWKS_VIRT_ADDR;
  swks_area.num_pages=SWKS_PAGES;
  //swks_area.map_proto = PROT_READ | PROT_WRITE;
  //swks_area.map_flags = MAP_FIXED;

  vm_register_user_mandatory_area(&swks_area);
}
