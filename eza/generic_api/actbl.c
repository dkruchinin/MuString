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

<<<<<<< .merge_file_i0BGdL
#include <acpi.h>
#include <mm/mm.h>
#include <mm/idalloc.h>

static struct acpi_rsdp *find_rsdp(ulong_t addr, uint32_t size)
=======
/*
 * The root system descriptor pointer is serached in the following areas:
 *
 * 1) In the bios rom at addresses 0xE0000 - 0xFFFFF
 * 2) In the first kylobite of EBDA
 */

#include <actbl.h.h>
#include <mm/mm.h>
#include <mm/idalloc.h>

#define PAGE_MASK ~(PAGE_SIZE - 1)
#define PAGE_OFFSET_MASK (PAGE_SIZE - 1)

#define BIOS_ROM 0xE0000
#define BIOS_ROM_SIZE 0x20000
#define RSDP_ALIGN 16
#define EBDA_POINTER_ADDR 0x40E

#define ACPI_BROKEN ((void*)~0UL)

static bool verify_checksum(uint8_t *buf, uint32_t size)
{
	uint8_t c = 0;
	int i;

	for (i = 0; i < size; i++)
		c += buf[i];

	kprintf("checksum = %d\n", c);

	return (c == 0);
}

static struct acpi_rsdp *find_rsdp(char *mem, uint32_t size)
>>>>>>> .merge_file_cYesVK
{
	char *p = mem + size;
	struct acpi_rsdp *ret = NULL;

	for (p = mem + size; mem < p; mem += RSDP_ALIGN) {
		if (!strncmp(mem, RSDP_SIGNATURE, sizeof(ret->signature)))
			break;
	}

	if (mem < p) {
		ret = (struct acpi_rsdp*)mem;
		/* verify base and extended checksums */
		if (!verify_checksum((uint8_t*)mem, RSDP_CHECKSUM_LEN))
			ret = (struct acpi_rsdp*)ACPI_BROKEN;	
		else if (ret->revision >= 2 &&
						 !verify_checksum((uint8_t*)(mem + RSDP_CHECKSUM_LEN), RSDP_EXT_CHECKSUM_LEN)) {
			
			ret = (struct acpi_rsdp*)ACPI_BROKEN;
		}
	}

	return ret;
}

static struct acpi_madt *find_madt(uint32_t *phys_addrs, int cnt)
{
	struct acpi_madt *madt;
	int i;
	uint8_t *p;

	for (i = 0; i < cnt; i++) {
		kprintf("Map physical address ox%x\n", phys_addrs[i]);
		p = mmap(NULL, PAGE_SIZE * 2, MMAP_PHYS, NOFD, (void*)(phys_addrs[i] & PAGE_MASK));
		if (p == NULL)
			abort();		

		madt = (struct acpi_madt*)(p + (phys_addrs[i] & PAGE_OFFSET_MASK));
		if (!strncmp(madt->header.signature, MADT_SIGNATURE,
								 sizeof(madt->header.signature))) {
			
			break;
		}
	}

	if (i == cnt)
		madt = NULL;

	return madt;
}

<<<<<<< .merge_file_i0BGdL
uint32_t get_acpi_lapic_structs(struct *madt_lapic, uint32_t size)
{
	uint32_t ret = 0;
=======
int get_acpi_lapic_structs(void *buf, uint32_t size)
{
	uint8_t *p, *p1 = (uint8_t*)buf;
	struct acpi_rsdp *rsdp;
	struct acpi_rsdt *rsdt;
	struct acpi_madt *madt;
	madt_lapic_t *lapic;
	uint32_t s;
	ulong_t ebda;
	
	/* search in the bios rom */
	p = mmap(NULL, BIOS_ROM_SIZE, MMAP_PHYS, NOFD, (void*)BIOS_ROM);
	if (p == NULL)
		abort();

	rsdp = find_rsdp((char*)p, BIOS_ROM_SIZE);
	if (rsdp == NULL) {
		/*search in EBDA*/
		p = mmap(NULL, PAGE_SIZE, MMAP_PHYS, NOFD, 0);
		if (p == NULL)
			abort();

		/* get the real-mode ebda physical pointer */
		ebda = *(uint16_t*)(p + EBDA_POINTER_ADDR);
		ebda <<= 4;

		p = mmap(NULL, PAGE_SIZE, MMAP_PHYS, NOFD, (void*)ebda);
		if (p == NULL)
			abort();

		rsdp = find_rsdp((char*)p, 1024);
	}

	if (rsdp != NULL) {
		kprintf("ACPI revision = %d\n", ret->revision);
		if (rsdp == ACPI_BROKEN) {
			kprintf("RSDP is broken!\n");
			return -1;
		}
	} else 
		return 0;

	kprintf("[%s]: line %d\n", __FUNCTION__, __LINE__);

	p = mmap(NULL, PAGE_SIZE * 2, MMAP_PHYS, NOFD, (void*)(rsdp->rsdt_addr & PAGE_MASK));
	if (p == NULL)
		abort();

	kprintf("[%s]: line %d\n", __FUNCTION__, __LINE__);	

	rsdt = (struct acpi_rsdt*)(p + (rsdp->rsdt_addr & PAGE_OFFSET_MASK));
	kprintf("[%s]: line %d; rsdt addr = 0x%x, tablen = %d\n", __FUNCTION__, __LINE__, rsdp->rsdt_addr, rsdt->header.len);
	if (verify_checksum((uint8_t*)rsdt, rsdt->header.len)) {
		kprintf("[%s]: line %d\n", __FUNCTION__, __LINE__);	
		s = (rsdt->header.len - sizeof(rsdt->header)) / sizeof(rsdt->tbl_addrs);
		madt = find_madt(rsdt->tbl_addrs, s);
	} else {
		kprintf("RSDT is broken!\n");
		return -1;
	}

	if (madt != NULL) {
		//kprintf("MADT is found, table size = %d!!!\n", madt->header.len);
		if (madt == ACPI_BROKEN) {
			kprintf("MADT is broken!\n");
			return -1;
		}
	} else
		return 0;

	p = (uint8_t*)madt + madt->header.len;
	lapic = (madt_lapic_t*)((ulong_t)madt + sizeof(struct acpi_madt));
	s = 0;
	while (lapic < (madt_lapic_t*)p && s < size) {
		if (lapic->header.type == LAPIC_TABLE_TYPE) {
			memcpy(p1 + s, lapic, sizeof(madt_lapic_t));
			s += sizeof(madt_lapic_t);
		}
		lapic = (madt_lapic_t*)((ulong_t)lapic + lapic->header.len);
	}

	//kprintf("%d local apic tables are found!!!\n", s / sizeof(madt_lapic_t));

	return s / sizeof(madt_lapic_t);
>>>>>>> .merge_file_cYesVK
}
