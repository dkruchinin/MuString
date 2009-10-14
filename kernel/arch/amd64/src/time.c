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
 * (c) Copyright 2006,2007,2008,2009 MString Core Team <http://mstring.jarios.org>
 * (c) Copyright 2008,2009 Tirra <madtirra@jarios.org>
 *
 * mstring/amd64/time.c: specific time operations
 *
 */

#include <arch/types.h>
#include <mstring/types.h>
#include <mstring/time.h>
#include <mstring/swks.h>

/* defined values for BIOS time */
/* RTC io ports */
#define RTC_ADDRESS 0x70
#define RTC_DATA 0x71
/* RTC registers */

/* Current time and date */
#define RTC_SECONDS 0x0
#define RTC_MINUTES 0x2
#define RTC_HOURS 0x4
#define RTC_DAY_OF_WEEK 0x6
#define RTC_DAY_OF_MONTH 0x7
#define RTC_MONTH 0x8
#define RTC_YEAR 0x9

/* Alarm control */
#define RTC_ALARM_SECONDS 0x1
#define RTC_ALARM_MINUTES 0x3
#define RTC_ALARM_HOURS 0x5

/* macros for calc */
#define BCD2BIN(x) ((x) & 0x0f) + ((x) >> 4) * 10
#define BIN2BCD(x) (((x) / 10) << 4) + (x) % 10

/* work around with BIOS clock */
static uint8_t rtc_read_register(uint8_t reg)
{
  outb(RTC_ADDRESS, reg);
  return inb(RTC_DATA);
}

static void rtc_write_register(uint8_t reg, uint8_t val)
{
  outb(RTC_ADDRESS, reg);
  outb(RTC_DATA, val);
}

static void get_rtc_time(struct tm *time)
{
  /* Overflow of seconds or minutes may happen,
   * so we check it to return consistent values.
   */
  for (;;) {
    time->tm_sec = rtc_read_register(RTC_SECONDS);
    time->tm_min = rtc_read_register(RTC_MINUTES);
    time->tm_hour = rtc_read_register(RTC_HOURS);
    if ((time->tm_min == rtc_read_register(RTC_MINUTES)) &&
        (time->tm_hour == rtc_read_register(RTC_HOURS)))
      break;
  }
  time->tm_mday = rtc_read_register(RTC_DAY_OF_MONTH);
  time->tm_mon = rtc_read_register(RTC_MONTH);
  time->tm_year = rtc_read_register(RTC_YEAR);

  time->tm_sec = BCD2BIN(time->tm_sec);
  time->tm_min = BCD2BIN(time->tm_min);
  time->tm_hour = BCD2BIN(time->tm_hour);
  time->tm_mday = BCD2BIN(time->tm_mday);
  time->tm_mon = BCD2BIN(time->tm_mon);
  time->tm_year = BCD2BIN(time->tm_year);
  time->tm_wday = BCD2BIN(time->tm_wday);

  if (time->tm_year <= 69)
    time->tm_year += 100;
  time->tm_mon--;

  time->tm_wday = 0;
  time->tm_yday = 0;
  time->tm_isdst = 0;
  return;
}

/* TODO: add write function, it may be called when shutdown going */

void arch_setup_time(void)
{
  get_rtc_time(&s_epoch);
}
