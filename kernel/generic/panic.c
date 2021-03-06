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
 *
 */

#include <mstring/stdarg.h>
#include <mstring/kprintf.h>
#include <mstring/kconsole.h>
#include <mstring/interrupt.h>
#include <mstring/panic.h>
#include <mstring/types.h>

#define PANIC_BUF_SIZE 1024

void panic_core(const char *fname, const char *fmt, ...)
{
  va_list ap;
  char panic_buf[PANIC_BUF_SIZE];

  interrupts_disable();
  if (!default_console()->is_enabled)
    default_console()->enable();

  va_start(ap, fmt);
  vsnprintf(panic_buf, sizeof(panic_buf), fmt, ap);
  va_end(ap);
  kprintf("\n========[!!PANIC!!]=========\n");
  kprintf("[%s]: %s\n", fname, panic_buf);  
  __stop_cpu();
}

