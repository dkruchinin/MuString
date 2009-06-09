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
 * (c) Copyright 2008 Dmitry Gromada <gromada@jarios.org>
 *
 * Fake acpi table parser to run the kernel on an emulator
 */

#include <arch/types.h>
#include <mstring/actbl.h>

int get_acpi_lapic_info(uint32_t *lapic_base, uint8_t *lapic_ids, int size, int *total_apics)
{
	int i;

	*total_apics = size;

	for (i = 0; i < size; i++)
		lapic_ids[i] = i;

	return size;
}
