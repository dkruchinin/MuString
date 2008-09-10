/* amd64 linker script 
 * preprocessed while compilation
 */

#include <config.h>
#include <eza/arch/boot.h>
#include <eza/arch/page.h>

OUTPUT_FORMAT("elf64-x86-64")

SECTIONS {
	.unmapped BOOT_OFFSET: AT (0) {
		unmapped_ktext_start = .;
		*(K_TEXT_START);
		unmapped_ktext_end = .;

		unmapped_kdata_start = .;
		*(K_DATA_START);
		*(K_INI_PTLS);
		unmapped_kdata_end = .;
	}

    _low_kernel_end = p2k(BOOT_OFFSET) + SIZEOF(.unmapped);

	.mapped(_low_kernel_end) : AT (SIZEOF(.unmapped)) {        
        _kernel_start = .;
		ktext_start = .;
		*(.text);
		ktext_end = .;

		kdata_start = .;
		*(.data);		/* initialized data */
		*(.rodata*);		/* string literals */
		hardcoded_load_address = .;
		QUAD(p2k(BOOT_OFFSET));
		hardcoded_ktext_size = .;
		QUAD(ktext_end - ktext_start + (unmapped_ktext_end - unmapped_ktext_start));
		hardcoded_kdata_size = .;
		QUAD(kdata_end - kdata_start + (unmapped_kdata_end - unmapped_kdata_start));
		hardcoded_unmapped_ktext_size = .;
		QUAD(unmapped_ktext_end - unmapped_ktext_start);
		hardcoded_unmapped_kdata_size = .;
		QUAD(unmapped_kdata_end - unmapped_kdata_start);
		*(COMMON);		/* global variables */

		*(.eh_frame);

		symbol_table = .;
		*(symtab.*);            /* Symbol table, must be LAST symbol!*/

		*(.bss);		/* uninitialized static variables */

		kdata_end = .;
	}

#ifdef CONFIG_SMP
    .percpu_data (kdata_end) : AT (k2p(kdata_end)) {
       *(.percpu_data);
    }

    _hardcoded_unmapped_size = (unmapped_ktext_end - unmapped_ktext_start) + (unmapped_kdata_end - unmapped_kdata_start);
    ap_boot = unmapped_ap_boot - (BOOT_OFFSET + AP_BOOT_OFFSET);
    ap_gdtr = unmapped_ap_gdtr - (BOOT_OFFSET + AP_BOOT_OFFSET);
    protected_ap_gdtr = p2k(ap_gdtr);
#endif
   
   . = ALIGN(0x1000);
   _kernel_end = .;	
}
