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
 * (c) Copyright 2008 Tirra <madtirra@jarios.org>
 * (c) Copyright 2008 Dmitry Gromada <gromada@jarios.org>
 *
 * mstring/amd64/platform.c: specific platform initialization.
 *
 */

#include <config.h>
#include <arch/types.h>
#include <arch/apic.h>
#include <mm/page.h>
#include <mm/page_alloc.h>
#include <mstring/smp.h>
#include <mstring/kprintf.h>
#include <mstring/timer.h>
#include <mstring/unistd.h>
#include <mstring/swks.h>
#include <mstring/types.h>

static const int mon_days[12] = {
  31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

static ulong_t __init_tsepoch(void)
{
  ulong_t r=0; /* -1900 */
  int i;

  r=s_epoch.tm_sec + s_epoch.tm_min * 60 + s_epoch.tm_hour*3600 +
    (s_epoch.tm_mday-1)*86400;

  /* month */
  for(i=0;i<s_epoch.tm_mon;i++) {
    r+=mon_days[i]*86400;
    if((i==1) && (s_epoch.tm_year % 4 ==2 ))
      r+=86400;
  }

  /* year */
  for(i=0;i<s_epoch.tm_year;i++) {
    r+=365*86400;
    if(i % 4 == 2) r +=86400;
  }

  return r;
}

void arch_initialize_swks(void)
{
 /* According to the x86 programming manual, we must reserve
  * eight bits after the last available I/O port in all IOPMs.
  */
  swks.ioports_available=TSS_IOPORTS_LIMIT;
  swks.secs_since_epoch=__init_tsepoch();
}
