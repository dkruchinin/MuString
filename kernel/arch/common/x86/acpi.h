#ifndef __MSTRING_ARCH_ACPI_H__
#define __MSTRING_ARCH_ACPI_H__

#include <mstring/types.h>

typedef struct acpi_rsdp {
  uint8_t signature[8];
  uint8_t checksum;
  uint8_t oemid[6];
  uint8_t revision;
  uint32_t rsdt_addr;
  uint32_t length;
  uint64_t xsdt_addr;
  uint8_t ext_checksum;
  uint8_t reserved[3];
} __attribute__ ((packed)) acpi_rsdp_t;

typedef struct acpi_table_hdr {
  uint8_t signature[4];
  uint32_t length;
  uint8_t revision;
  uint8_t checksum;
  uint8_t oemid[6];
  uint64_t oem_table_id;
  uint32_t oem_revision;
  uint32_t creator_id;
  uint32_t creator_rev;
} __attribute__ ((packed)) acpi_table_hdr_t;

typedef struct acpi_sdt {
  acpi_table_hdr_t hdr;
  uint8_t entry[];
} acpi_sdt_t;

typedef struct acpi_fadt {
  acpi_table_hdr_t hdr;
  uint32_t firmware_ctrl;
  uint32_t dsdt; /* offsest: 40 */
  uint32_t pad[8];
  uint32_t pm_timer_blk;
} __attribute__ ((packed)) acpi_fadt_t;

typedef struct acpi_madt {
  acpi_table_hdr_t hdr;
  uint32_t lapic_addr;     /* Local APIC physical address */
  uint32_t flags;          /* Multiple APIC flags */
  
  /*
   * A list of APIC structures including all of the
   * I/O APIC, I/O SAPIC, Local APIC, Local SAPIC, ISO, NMI src,
   * Local APIC NMI src, Local APIC address Override.
   */
  uint8_t apic_structs[];
} __attribute__ ((packed)) acpi_madt_t;

typedef struct madt_hdr {
  uint8_t type;
  uint8_t length;
} __attribute__ ((packed)) madt_hdr_t;

typedef struct madt_lapic {
  madt_hdr_t hdr;
  uint8_t acpi_proc_id;
  uint8_t apic_id;
  struct {
    unsigned enabled  :1;
    unsigned reserved :31;
  } flags;
} __attribute__ ((packed)) madt_lapic_t;

typedef enum acpi_tlbid {
  MADT_TABLE = 0,
  FADT_TABLE,
} acpi_tlbid_t;

typedef enum madt_type {
  MADT_LAPIC = 0,
  MADT_IO_APIC,
  MADT_ISO,
  MADT_NMI,
  MADT_LAPIC_NMI,
  MADT_LAPIC_AOS,
  MADT_IO_SAPIC,
  MADT_LSAPIC,
  MADT_PIS,
} madt_type_t;

#define ACPI_TLB_SIG_BYTES 4
#define ACPI_EBDA_BASE 0x40E
#define ACPI_SIG_BYTES 8
#define ACPI_SIG       "RSD PTR "
#define ACPI_RSDT_SIG  "RSDT"
#define ACPI_XSDT_SIG  "XSDT"

INITCODE void acpi_init(void);
void *acpi_getaddr(uintptr_t phys_addr, size_t length);
void *acpi_find_table(acpi_tlbid_t type);
void *madt_find_table(acpi_madt_t *madt, madt_type_t type, int num);

#endif /* __MSTRING_ARCH_ACPI_H__ */
