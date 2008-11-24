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
 * include/eza/actbl.h: minimal acpi config table definition
 */

#ifndef __ACTBL_H__
#define __ACTBL_H__

#include <eza/arch/types.h>

#define RSDP_SIGNATURE "RSD PTR "
#define RSDT_SIGNATURE "RSDT"
#define MADT_SIGNATURE "APIC"

#define LAPIC_TABLE_TYPE 0

struct acpi_rsdp {
	char signature[8];
	uint8_t checksum;
	char oemid[6];
	uint8_t revision;
	uint32_t rsdt_addr;
	uint32_t len;
	uint64_t xsdt_addr;
	uint8_t ext_checksum;
	uint8_t __res[3];
};

struct rsdt_header {
	char signature[4];
	uint32_t tablen;
	uint8_t revision;
	uint8_t checksum;
	char oemid[6];
	char oem_tabid[8];
	uint32_t oem_rev;
	uint32_t vendor_id;
	uint32_t vendor_rev;
};

struct acpi_rsdt {
	struct rsdt_header header;
	uint32_t tbl_addrs[1]; /* array of physical addresses that points to other headers */
};

struct madt_header {
	char signature[4];
	uint32_t len;
	uint8_t revision;
	uint8_t checksum;
	char oemid[6];
	char oem_tabid[8];
	uint32_t oem_rev;
	uint32_t vendor_id;
	uint32_t vendor_rev;
	uint32_t lapic_addr;
	uint32_t flags;
};

struct acpi_cfgtab_header {
	uint8_t type;
	uint8_t len;
};

struct madt_lapic {
	struct acpi_cgftab_header header;
	uint8_t cpuid;
	uint8_t apic_id;
	uint32_t flags;
};

struct acpi_madt {
	struct madt_header header;
	struct madt_lapic lapic[1];
};

/* 
 * get local apic description structures from the acpi configuration space
 * 
 * @[out] madt_lapic - buffer to store found local apic structures
 * @size - size of the buffer
 *
 * Return the number of stored local apic structures
 */
uint32_t get_acpi_lapic_structs(struct *madt_lapic, uint32_t size);

#endif
