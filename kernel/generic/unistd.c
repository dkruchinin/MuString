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
 * (c) Copyright 2008 MadTirra <tirra.newly@gmail.com>
 *
 * mstring/unistd.c: kernel implementation unified routines.
 *
 */

#include <arch/types.h>
#include <mstring/unistd.h>
#include <mstring/interrupt.h>
#include <arch/asm.h>

void atom_usleep(usec_t usecs)
{
  bool enabled=is_interrupts_enabled();

  if( enabled ) {
    interrupts_disable();
  }
  arch_delay_loop(delay_loop*usecs);
  if( enabled ) {
    interrupts_enable();
  }
}

void usleep(usec_t usecs)
{
  arch_delay_loop(delay_loop*usecs);
}

