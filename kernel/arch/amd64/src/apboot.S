.section bootstrap_text, "ax"
.code16
.globl ap_boot, ap_boot_start, ap_boot_end, mycall32
ap_boot_start:
ap_boot:
        cli
        cld
        
        movl $(ap_boot_stack - ap_boot_start + 0x9000), %esp
        lgdt ap_boot_gdt_descr - ap_boot_start + 0x9000
        lidt ap_boot_idt_descr - ap_boot_start + 0x9000

        movl %cr0, %eax
        orl $0x01, %eax
        movl %eax, %cr0

        ljmpw $8, $(mycall32 - ap_boot_start + 0x9000)

.code32
mycall32:
        movw %cs, %ax
        movw %ax, %ds
        movw %ax, %es
        movw %ax, %gs
        movw %ax, %fs
        movw %ax, %gs

        pushl $8
        pushl ap_jmp_rip - ap_boot_start + 0x9000
        lretl

.globl ap_jmp_rip
.align 4
ap_jmp_rip:
        .long 0
        
.code16
ap_boot_gdt:
        .quad   0x0000000000000000  /* NULL descriptor */        
        .quad   0x00cf9b000000ffff  /* 32bit Code segment */
        .quad   0x00cf93000000ffff  /* 32bit Data segment */
ap_boot_gdt_end:

ap_boot_gdt_descr:
        .word ap_boot_gdt_end - ap_boot_gdt
        .long 0x9000 + ap_boot_gdt - ap_boot_start

ap_boot_idt_descr:
        .word 0xffff
        .long 0x0

ap_boot_stack_end:
        .fill 64, 8, 0
ap_boot_stack:        
ap_boot_end: