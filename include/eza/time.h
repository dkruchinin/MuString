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
 * include/eza/time.h: contains types and prototypes for dealing with
 *                     time processing.
 *
 */

#ifndef __TIME_H__
#define __TIME_H__ 

#include <eza/arch/types.h>
#include <eza/swks.h>

typedef long clock_t;

typedef enum {
  CLOCK_REALTIME=0,
} clockid_t;

uint32_t delay_loop;

void timer_tick(void);

typedef struct __timeval {
  ulong_t tv_sec;   /* seconds */
  ulong_t tv_nsec;  /* microseconds */
} timeval_t;

typedef struct itimerspec {
  timeval_t it_interval,it_value;
} itimerspec_t;

#define system_ticks  swks.system_ticks_64

#define TIMER_ABSTIME  0x1

typedef enum __posix_timer_command {
  __POSIX_TIMER_SETTIME=0,
  __POSIX_TIMER_GETTIME=1,
  __POSIX_TIMER_GETOVERRUN=2,
} posix_timer_command_t;

#define NANOSLEEP_MAX_SECS   1000000000
#define NANOSLEEP_MAX_NSECS  1000000000

#define timeval_is_valid(t)  ((t)->tv_sec < NANOSLEEP_MAX_SECS &&       \
                              (t)->tv_nsec < NANOSLEEP_MAX_NSECS )

#define time_to_ticks(_tv)  (timeval_is_valid((_tv)) ? (_tv)->tv_sec*HZ + (_tv)->tv_nsec/(NANOSLEEP_MAX_NSECS/HZ) : 0 )

#endif

