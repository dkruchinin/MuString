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
 * (c) Copyright 2005,2008 Tirra <madtirra@jarios.org>
 *
 * server/server.c: servers going multiboot functions
 *
 */

#include <eza/arch/types.h>
#include <mlibc/kprintf.h>
#include <server.h>

uint32_t server_get_num(void)
{
  return init.c;
}

uintptr_t server_get_start_phy_addr(void)
{
  if(server_get_num()>0) 
    return init.server[0].addr;
  else
    return 0;
}

uintptr_t server_get_end_phy_addr(void)
{
  int i=server_get_num();

  if(i>0) {
    if(i>MAX_PRIVBOOT_SERVERS)      i=MAX_PRIVBOOT_SERVERS;
    return init.server[i-1].addr+init.server[i-1].size;
  } else
      return 0;
}


