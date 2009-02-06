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
 * (c) Copyright 2005,2008 Tirra <tirra.newly@gmail.com>
 *
 * eza/generic_api/syscall.c: syscall related functions
 *                            some useful system bindings
 *
 */

#include <eza/arch/types.h>
#include <eza/arch/asm.h>

/* real syscall handler */
/* TODO: fix it and implement */
uintptr_t syscall_handler(uintptr_t yy0,uintptr_t yy1,uintptr_t yy2,
			  uintptr_t yy3,uintptr_t yy4,uintptr_t yy5)
{
  uintptr_t o;

  return o;
}

