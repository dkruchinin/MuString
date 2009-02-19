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
 * eza/generic_api/swks.c: Contains functions for dealing with "SWKS" -
 *                         "System-Wide Kernel Statistics", a global structure
 *                         that contains all kernel statistics visible to
 *                         all userspace applications.
 *
 */

#include <mlibc/types.h>
#include <eza/arch/timer.h>
#include <eza/swks.h>
#include <mm/page.h>
#include <mm/vmm.h>
#include <mlibc/kprintf.h>
#include <mlibc/string.h>
#include <eza/arch/mm.h>

/* Here it is ! */
swks_t __page_aligned__ swks;

void initialize_swks(void)
{
  memset(&swks, 0, sizeof(swks));

  swks.system_clock_ticks = INITIAL_TICKS_VALUE;
  swks.nr_cpus = CONFIG_NRCPUS;
  swks.hz = HZ;
  swks.num_irqs=NUM_IRQS;
  arch_initialize_swks();
}
