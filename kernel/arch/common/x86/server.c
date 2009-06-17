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
 */

#include <config.h>
#include <server.h>
#include <arch/boot.h>
#include <mm/page.h>
#include <mstring/assert.h>
#include <mstring/panic.h>
#include <mstring/types.h>

static int get_num_servers(void)
{
  return mb_info->mods_count;
}

static uintptr_t get_start_addr(void)
{
  return (uintptr_t)mb_info->mods_addr;
}

static uintptr_t get_end_addr(void)
{
  multiboot_mod_t *mods;

  if (!mb_info->mods_count) {
    return 0;
  }
  
  mods = (multiboot_mod_t *)PHYS_TO_KVIRT((uintptr_t)mb_info->mods_addr);
  mods += mb_info->mods_count - 1;
  return (uintptr_t)mods->mod_end;
}

static void server_by_num(int num, init_server_t *serv)
{
  multiboot_mod_t *mod =
    (multiboot_mod_t *)PHYS_TO_KVIRT((uintptr_t)mb_info->mods_addr);
  
  ASSERT(serv != NULL);
  ASSERT((num >= 0) && (num < mb_info->mods_count));

  mod += num;
  serv->addr = mod->mod_start;
  serv->size = mod->mod_end - mod->mod_start;
  serv->name = (char *)(uintptr_t)mod->string;
}

static struct server_ops x86_server_ops = {
  .get_num_servers = get_num_servers,
  .get_start_addr = get_start_addr,
  .get_end_addr = get_end_addr,
  .get_server_by_num = server_by_num,
};

INITCODE void arch_servers_init(void)
{
  int num;
  
  ASSERT(server_ops == NULL);
  server_ops = &x86_server_ops;
  num = server_ops->get_num_servers();
  if (num) {
    uintptr_t start, end;

    start = server_ops->get_start_addr();
    end = server_ops->get_end_addr();
    if (B2MB(end - start) > CONFIG_MAX_SRV_MBS) {
      panic("Total size of services exeeds configuration limit. "
            "Total size = %dM, configuration limit = %dM.",
            B2MB(end - start), CONFIG_MAX_SRV_MBS);
    }
  }
}

