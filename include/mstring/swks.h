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
 * (c) Copyright 2008 MadTirra <madtirra@jarios.org>
 *
 * include/mstring/swks.h: Contains types and prototypes for dealing with "SWKS" -
 *                    the "System-Wide Kernel Statistics", a global structure
 *                    that contains all kernel statistics visible to
 *                    all userspace applications.
 *
 */

#ifndef __SWKS_H__
#define __SWKS_H__ 

#include <mstring/types.h>
#include <mstring/interrupt.h>
#include <mstring/scheduler.h>

enum __swks_constants {
  INITIAL_TICKS_VALUE = 0,
};

/* Per-CPU global statistics */
typedef struct __cpu_stats {
  scheduler_cpu_stats_t sched_stats;
    //uint64_t irq_stat[NUM_IRQS];
} cpu_stats_t;

/* System-Wide Kernel Statistics. */
typedef struct __swks {
  /* Time-related statistics. */
  ulong_t  system_clock_ticks; /* Number of timer clocks occured since uptime */
  ulong_t  hz,secs_since_epoch;
  ulong_t  num_irqs;
  ulong_t  ioports_available;

  /* CPU-related statistics. */
  ulong_t  nr_cpus;
  cpu_stats_t cpu_stat[CONFIG_NRCPUS];
} swks_t;


/* The singleton of the SWKS object. */
extern swks_t swks;

void initialize_swks(void);
void arch_initialize_swks(void);
long get_swks_virtual_address(void);

#define SWKS_PAGES  ((sizeof(swks)>>PAGE_WIDTH)+1)

#endif

