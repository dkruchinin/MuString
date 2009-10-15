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
 * (c) Copytight 2009 Dan Kruchinin <dk@jarios.org>
 *
 */

#include <mm/page.h>
#include <mm/mem.h>
#include <arch/acpi.h>
#include <arch/cpu.h>
#include <mstring/string.h>
#include <mstring/kprintf.h>
#include <mstring/panic.h>
#include <mstring/types.h>

static acpi_rsdp_t *acpi_rsdp = NULL;
static acpi_sdt_t *acpi_rsdt = NULL;
static acpi_sdt_t *acpi_xsdt = NULL;
static char *table_sigs[] = {
  "APIC", /* MADT_TABLE */
  "FACP", /* FADT_TABLE */
};

static uint8_t get_acpi_checksum(void *ptr, int size)
{
  uint8_t checksum = 0, i;
  uint8_t *p = ptr;

  for (i = 0; i < size; i++) {
    checksum += *p;
    p++;
  }

  return checksum;
}

static acpi_rsdp_t *scan_rsdp(uint32_t pmem_start, uint32_t pmem_length)
{
  acpi_rsdp_t *rsdp_ret, *rsdp_tmp;
  uintptr_t pa, pmem_end;

  rsdp_ret = NULL;
  pmem_end = pmem_start + pmem_length;
  pa = (uintptr_t)acpi_getaddr(PHYS_TO_KVIRT(pmem_start),
                               pmem_end - pmem_start);

  for (pmem_end = PHYS_TO_KVIRT(pmem_end); pa < pmem_end; pa += 16) {
    rsdp_tmp = (acpi_rsdp_t *)pa;
    if (memcmp(rsdp_tmp->signature, ACPI_SIG, ACPI_SIG_BYTES)) {
      continue;
    }
    if (get_acpi_checksum(rsdp_tmp, 20) != 0) {
      continue;
    }

    rsdp_ret = rsdp_tmp;
    break;
  }

  return rsdp_ret;
}

static INITCODE acpi_sdt_t *acpi_get_xsdt(void)
{
  acpi_sdt_t *xsdt;

  ASSERT(acpi_rsdp != NULL);
  if (acpi_rsdp->revision < 2) {
    goto no_xsdt;
  }

  xsdt = acpi_getaddr(PHYS_TO_KVIRT(acpi_rsdp->xsdt_addr), sizeof(acpi_sdt_t));
  if (memcmp(xsdt->hdr.signature, ACPI_XSDT_SIG, 4)) {
    goto no_xsdt;
  }
  if (get_acpi_checksum(acpi_rsdp, 36) != 0) {
    kprintf("Invalid checksum: %d\n", get_acpi_checksum(acpi_rsdp, 36));
    goto no_xsdt;
  }

  xsdt = acpi_getaddr((uintptr_t)xsdt, xsdt->hdr.length);
  return xsdt;
no_xsdt:
  return NULL;
}

static INITCODE acpi_sdt_t *acpi_get_rsdt(void)
{
  acpi_sdt_t *rsdt;

  ASSERT(acpi_rsdp != NULL);
  rsdt = acpi_getaddr(PHYS_TO_KVIRT(acpi_rsdp->rsdt_addr), sizeof(acpi_sdt_t));
  if (memcmp(rsdt->hdr.signature, ACPI_RSDT_SIG, 4)) {
    return NULL;
  }

  return acpi_getaddr((uintptr_t)rsdt, rsdt->hdr.length);
}

acpi_rsdp_t *acpi_rsdp_find(void)
{
  acpi_rsdp_t *rsdp;

  rsdp = scan_rsdp(ACPI_EBDA_BASE, 0x400);
  if (!rsdp) {
    rsdp = scan_rsdp(0xe0000, 0x20000);    
  }
  
  return rsdp;
}

void *acpi_find_table(acpi_tlbid_t type)
{
  acpi_table_hdr_t *table_hdr;
  acpi_sdt_t *sdt;
  size_t entry_len;
  int num_entries, i;
  uintptr_t table_addr;

  if (unlikely(!acpi_rsdp)) {
    return NULL;
  }

  entry_len = 8;
  sdt = acpi_xsdt;
  if (!sdt) {
    if (unlikely(!acpi_rsdt)) {
      return NULL;
    }

    sdt = acpi_rsdt;
    entry_len = 4;
  }

  num_entries = (sdt->hdr.length - sizeof(sdt->hdr)) / entry_len;
  for (i = 0; i < num_entries; i++) {
    table_addr = 0;
    memcpy(&table_addr, sdt->entry + i * entry_len, entry_len);
    table_hdr = acpi_getaddr(PHYS_TO_KVIRT(table_addr), sizeof(*table_hdr));
    if (!memcmp(table_hdr->signature, table_sigs[type], ACPI_TLB_SIG_BYTES)) {
      return table_hdr;
    }
  }

  return NULL;
}

#include <arch/asm.h>
void *acpi_getaddr(uintptr_t virt_addr, size_t length)
{
  uintptr_t page = PAGE_ALIGN_DOWN(virt_addr);
  uintptr_t end_addr = PAGE_ALIGN(virt_addr + length);
  int ret;

  while (page < end_addr) {
    if (!page_is_mapped(KERNEL_ROOT_PDIR(), page)) {
      ret = mmap_page(KERNEL_ROOT_PDIR(), page,
                      virt_to_pframe_id((void *)page),
                      KMAP_KERN | KMAP_READ | KMAP_WRITE | KMAP_NOCACHE);
      write_cr3(read_cr3());
      if (ret) {
        panic("Failed to map address %p for ACPI: ENOMEM.", page);
      }
    }

    page += PAGE_SIZE;
  }

  return (void *)virt_addr;
}

void *madt_find_table(acpi_madt_t *madt, madt_type_t type, int num)
{
  madt_hdr_t *hdr, *madt_end;

  if (type > MADT_PIS) {
    panic("Unknown type of ACPI MADT table: %d. "
          "expected types range: [%d, %d]\n", type, MADT_LAPIC, MADT_PIS);
  }

  hdr = (madt_hdr_t *)madt->apic_structs;
  madt_end = (madt_hdr_t *)((char *)madt + madt->hdr.length);
  while (hdr < madt_end) {
    if (hdr->type == type) {
      if (--num == 0) {
        return hdr;
      }
    }

    hdr = (madt_hdr_t *)((char *)hdr + hdr->length);
  }

  return NULL;
}

INITCODE int acpi_init(void)
{
  acpi_rsdp = acpi_rsdp_find();
  if (!acpi_rsdp) {
    return -1;
  }

  acpi_rsdt = acpi_get_rsdt();
  acpi_xsdt = acpi_get_xsdt();
  return 0;
}
