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

/*
 * The root system descriptor pointer is searched in the following areas:
 *
 * 1) In the bios rom at addresses 0xE0000 - 0xFFFFF
 * 2) In the first kilobyte of EBDA
 */

#include <arch/types.h>
#include <mm/page.h>
#include <mm/idalloc.h>
#include <mm/vmm.h>
#include <mstring/errno.h>
#include <mstring/actbl.h>

#define BIOS_ROM 0xE0000
#define BIOS_ROM_SIZE 0x20000
#define RSDP_ALIGN 16
#define EBDA_POINTER_ADDR 0x40E

#define ACPI_BROKEN ((void*)~0UL)

#define MAX_MAPPED_PAGES 32
#define EXTRA_PAGES 4

static int mapped_pages = 0;
extern idalloc_meminfo_t idalloc_meminfo;
extern page_frame_t *kernel_root_pagedir;

static bool phys_range_is_mapped(int start_idx, int n)
{
	uintptr_t va;
	int i, j = start_idx + n;
	page_frame_t *frame;

	for (i = start_idx; i < j; i++) {
		va = (uintptr_t)pframe_id_to_virt(i);
		frame = pframe_by_number(i);
		if (!page_is_mapped(&kernel_rpd, PAGE_ALIGN_DOWN(va)))
			break;
	}

	return (i == j);
}

static int map_acpi_tables(uint32_t *phys_addrs, int naddrs, uintptr_t va, uint32_t *base_addr)
{
	int i, m;
	long idx, sidx = 0x7FFFFFFF, eidx = -1;

	for (i = 0; i < naddrs; i++) {
		idx = phys_addrs[i] >> PAGE_WIDTH;
		if (idx < sidx) {            
			if (eidx - idx >= MAX_MAPPED_PAGES) {
				break;
            }

			sidx = idx;
		}
		if (idx > eidx) {            
			if (idx - sidx >= MAX_MAPPED_PAGES)
				break;

			eidx = idx;
		}
	}

	/* extra pages to map fully trailing table */
	eidx += EXTRA_PAGES;

	m = eidx - sidx + 1;
	*base_addr = sidx << PAGE_WIDTH;
	if (i) {
        int ret;
		/* map needed physical memory */
        ret = mmap_kern(va, sidx, m, KMAP_KERN | KMAP_READ | KMAP_NOCACHE);
	}
	if (m > mapped_pages)
		mapped_pages = m;

	return i;
}

static bool verify_checksum(uint8_t *buf, uint32_t size)
{
	uint8_t c = 0;
	int i;

	for (i = 0; i < size; i++)
		c += buf[i];

	return (c == 0);
}

static struct acpi_rsdp *find_rsdp(char *mem, uint32_t size)
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

static struct acpi_madt *find_madt(uint32_t *phys_addrs, int cnt, uintptr_t va)
{
	struct acpi_madt *madt = NULL;
	struct acpi_tab_header *h;
	int i = 0, j;
	uint8_t *p;
	uint32_t base;

	while (i < cnt && madt == NULL) {
		if (!phys_range_is_mapped(phys_addrs[i] >> PAGE_WIDTH, EXTRA_PAGES)) {
			j = map_acpi_tables(phys_addrs + i, cnt - i, va, &base);
            if (!j) {
                break;
            }
            
			p = (uint8_t*)va;
		} else {
			j = 1;
			base = phys_addrs[i] & ~PAGE_MASK;
			p = pframe_id_to_virt(phys_addrs[i] >> PAGE_WIDTH);
		}

		for (j += i; i < j; i++) {
			h = (struct acpi_tab_header*)(p + (phys_addrs[i] - base));
			if (!strncmp(h->signature, MADT_SIGNATURE,
									sizeof(madt->header.signature))) {
		
				if (verify_checksum((uint8_t*)h, h->len))
					madt = (struct acpi_madt*)h;
				else
					madt = (struct acpi_madt*)ACPI_BROKEN;

				break;
			}
		}
	}

	return madt;
}

int get_acpi_lapic_info(uint32_t *lapic_base, uint8_t *lapic_ids, int size, int *total_apics)
{
	uint8_t *p;
	struct acpi_rsdp *rsdp;
	struct acpi_rsdt *rsdt;
	struct acpi_madt *madt;
	madt_lapic_t *lapic;
	uintptr_t va, va1;
	int ret = 0, s;
	
	kprintf("Parse ACPI table\n");	

	/* search in the bios rom */
	p = pframe_id_to_virt(BIOS_ROM >> PAGE_WIDTH);

	rsdp = find_rsdp((char*)p, BIOS_ROM_SIZE);
	if (rsdp == NULL) {
		/*search in EBDA*/
		p = pframe_id_to_virt(0);

		/* get the real-mode ebda physical pointer */
		s = *(uint16_t*)(p + EBDA_POINTER_ADDR);
		s >>= (PAGE_WIDTH - 4);

		p = pframe_id_to_virt(s);
		rsdp = find_rsdp((char*)p, 1024);
	}

	if (rsdp != NULL) {
		kprintf("ACPI revision = %d\n", rsdp->revision);
		if (rsdp == ACPI_BROKEN) {
			kprintf("RSDP is broken!\n");
			return -EINVAL;
		}
	} else 
		return 0;

    /*
     * FIXME: actually it's not valid to use __allocate_vregion
     * from arch-independent code. __allocate_vregion is, strictly
     * speaking, architecture-dependent function, thus virtual address
     * for ACPI *should not* be allocated right here.
     */
	va = __allocate_vregion(MAX_MAPPED_PAGES + EXTRA_PAGES * 2);
	if (!va)
		return -ENOMEM;

	va1 = va + PAGE_SIZE * EXTRA_PAGES;	
	s = rsdp->rsdt_addr >> PAGE_WIDTH;
	if (!phys_range_is_mapped(s, EXTRA_PAGES)) {
		if (mmap_kern(va, s, EXTRA_PAGES, KMAP_KERN | KMAP_READ | KMAP_NOCACHE) < 0)
			return -ENOMEM;

		p = (uint8_t*)va;
	} else
		p = pframe_id_to_virt(s);

	rsdt = (struct acpi_rsdt*)(p + (rsdp->rsdt_addr & PAGE_MASK));
	if (verify_checksum((uint8_t*)rsdt, rsdt->header.len)) {
		s = (rsdt->header.len - sizeof(rsdt->header)) / sizeof(rsdt->tbl_addrs);
		madt = find_madt(rsdt->tbl_addrs, s, va1);
	} else {
		kprintf("RSDT is broken!\n");
		ret = -EINVAL;
		goto out;
	}

	if (madt != NULL) {
		if (madt == ACPI_BROKEN) {
			kprintf("MADT is broken!\n");
			ret = -EINVAL;
			goto out;
		}
	} else
		goto out;

	*lapic_base =  madt->lapic_addr;
	p = (uint8_t*)madt + madt->header.len;
	lapic = (madt_lapic_t*)((ulong_t)madt + sizeof(struct acpi_madt));
	s = 0;
	while (lapic < (madt_lapic_t*)p) {
		if ((lapic->header.type == LAPIC_TABLE_TYPE) && (lapic->flags & LAPIC_ENABLED)) {
			if (s < size)
				lapic_ids[s] = lapic->apic_id;
			
			s++;
		}
		
		lapic = (madt_lapic_t*)((ulong_t)lapic + lapic->header.len);
	}

	ret = (s > size) ? size : s;
	*total_apics = s;

out:
	/* Fixme: free allocated resources */
	return ret;
}
