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
 * include/server.h: servers going multiboot related structs and defines
 *
 */

#ifndef __SERVER_H__
#define __SERVER_H__

#include <mlibc/types.h>

#define MAX_PRIVBOOT_SERVERS  16

typedef struct __init_server {
  uintptr_t addr; /* start address of the server loaded via boot loader */
  size_t size; /* it's size */
} init_server_t;

typedef struct __init_type {
  ulong_t c; /* # of servers */
  init_server_t server[MAX_PRIVBOOT_SERVERS];
} init_t; /* general init servers structure */

extern init_t init;

/* functions */
uint32_t server_get_num(void); /* get real number of servers */
uintptr_t server_get_start_phy_addr(void); /* get start physical address */
uintptr_t server_get_end_phy_addr(void); /* get end physical address */

/* initing servers */
void server_run_tasks(void);

#endif /* __SERVER_H__ */
