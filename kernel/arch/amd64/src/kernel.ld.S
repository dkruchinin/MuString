#include <arch/page.h>

OUTPUT_FORMAT("elf64-x86-64")
OUTPUT_ARCH("i386:x86-64")
ENTRY(_start)

SECTIONS {
         . = KPHYS_START;
         __bootstrap_start = .;
         bootstrap_text : AT(ADDR(bootstrap_text) - KPHYS_START)
         {
                *(bootstrap_text);
         }

         bootstrap_data : AT(ADDR(bootstrap_data) - KPHYS_START)
         {
                *(bootstrap_data);                                
         }

         . = ALIGN(0x1000);
         __bootstrap_end = .;
         . = KERNEL_OFFSET + __bootstrap_end;
         .text : AT(ADDR(.text) - KERNEL_OFFSET - KPHYS_START)
         {
                *(.text);
         }

         .data : AT(ADDR(.data) - KERNEL_OFFSET - KPHYS_START)
         {
                *(.data);
                *(.rodata);
         }

         . = ALIGN(8);
         __ex_table_start = .;
         .ex_table : AT(ADDR(.ex_table) - KERNEL_OFFSET - KPHYS_START)
         {
                *(.ex_table);
         }
         __ex_table_end = .;

         __bss_start = .;
         .bss : AT(ADDR(.bss) - KERNEL_OFFSET - KPHYS_START)
         {
                *(.bss);
         }

         __bss_end = .;
         . = ALIGN(0x1000);
         __init_start = .;

         initdata : AT(ADDR(initdata) - KERNEL_OFFSET - KPHYS_START)
         {
                *(initdata);
         }

         initcode : AT(ADDR(initcode) - KERNEL_OFFSET - KPHYS_START)
         {
                *(initcode);
         }

         . = ALIGN(0x1000);
         __init_end = .;

         /DISCARD/ :
         {
                *(.comment);
                *(.ehframe*);
                *(.eh_frame*);
         }

         _kernel_end = .;
}
