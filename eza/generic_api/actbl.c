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
 * (c) Copyright 2006,2007,2008 Jari OS Core Team <http://jarios.org>
 * (c) Copyright 2008 Dmitry Gromada <gromada82@gmail.com>
 *
 * Scan scan acpi configuration tables for correct smp support
 */

#include <acpi.h>
#include <mm/mm.h>
#include <mm/idalloc.h>

static struct acpi_rsdp *find_rsdp(ulong_t addr, uint32_t size)
{
	struct acpi_rsdp *ret = NULL;
}

uint32_t get_acpi_lapic_structs(struct *madt_lapic, uint32_t size)
{
	uint32_t ret = 0;
}
