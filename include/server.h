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

#ifndef __MSTRING_SERVER_H__
#define __MSTRING_SERVER_H__

#include <mstring/types.h>

typedef struct init_server {
  uintptr_t addr; /* start address of the server loaded via boot loader */
  size_t size;   /* it's size */
  char *name;
} init_server_t;

struct server_ops {
  int (*get_num_servers)(void);
  uintptr_t (*get_start_addr)(void);
  uintptr_t (*get_end_addr)(void);
  void (*get_server_by_num)(int num, /* OUT */ init_server_t *serv);
};

extern struct server_ops *server_ops;

/* initing servers */
INITCODE void arch_servers_init(void);
INITCODE void server_run_tasks(void);

#endif /* __MSTRING_SERVER_H__ */
