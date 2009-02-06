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
 * (c) Copyright 2008 Michael Tsymbalyuk <mtzaurus@gmail.com>
 *
 * eza/generic_api/security.c: security check procedures live here.
 */

#include <eza/arch/types.h>
#include <eza/security.h>
#include <eza/arch/current.h>
#include <eza/process.h>

static bool def_check_process_control(task_t *target,ulong_t cmd, ulong_t arg)
{
  return true;
}

static bool def_check_create_process(task_creation_flags_t flags)
{
  return true;
}

static bool def_check_scheduler_control(task_t *target,ulong_t cmd, ulong_t arg)
{
  return true;
}

static bool def_check_access_ioports(task_t *target,ulong_t start_port,
                                     ulong_t end_port)
{
  return true;
}

static security_operations_t def_sops = {
  .check_process_control = def_check_process_control,
  .check_create_process = def_check_create_process,
  .check_scheduler_control = def_check_scheduler_control,
  .check_access_ioports = def_check_access_ioports,
};

security_operations_t *security_ops = &def_sops;
