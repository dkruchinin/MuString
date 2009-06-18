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
 * (c) Copyright 2006,2007,2008 MString Core Team <http://mstring.jarios.org>
 * (c) Copyright 2009 Dan Kruchinin <dk@jarios.org>
 *
 */

#include <arch/seg.h>
#include <arch/cpu.h>
#include <arch/smp.h>

/*
 * Intel MP specification says that the AP-initialization code
 * must be located in one of the lowest possible pages(i.e. in the
 * real-mode address space (< 1M)). By default the code you're reading
 * is located in the bootstrap_text section(i.e. higher than 1M). In SMP
 * cofiguration we reserve one of the lowest available pages during low-level
 * memory manager initialization. Then before waking up of APs we copy the code
 * from here to the reserved earlier page that will be used by APs for loading
 * bootup code.
 */
        
.section bootstrap_text, "ax"
.code16
.globl ap_boot, ap_boot_start, ap_boot_end, ap32_entry, ap_config
ap_boot_start:
ap_boot:
        cli
        cld

        /*
         * NOTE: boot_gdt_descr, page_addr and ap_boot_gdt itself
         * are filled with proper values before code is copied to reserved
         * page.
         */
        lgdt %cs:AP_OFFSET(boot_gdt_descr)
        movl %cs:AP_OFFSET(page_addr), %eax
        lidt %cs:AP_OFFSET(ap_boot_idt_descr)
        movl $AP_OFFSET(ap_boot_stack), %esp
        addl %cs:AP_OFFSET(page_addr), %esp

        /* Enable protected mode */
        movl %cr0, %eax
        orl $CR0_PE, %eax
        movl %eax, %cr0

        /*
         * Finally we can jump to 32bit code via properly cofigure
         * kernel code segment. It must have base address equal to
         * physical address of reserved earlier page (i.e. page_addr field)
         */
        .byte 0xea
        .word AP_OFFSET(ap32_entry)
        .word GDT_SEL(APGDT_KCOFF_DESCR)

.code32
ap32_entry:
        movw %cs, %ax
        movw %ax, %ds
        movw %ax, %es
        movw %ax, %gs
        movw %ax, %fs
        movw %ax, %gs

        /*
         * Push *normal*(with base address = 0) code segment on the
         * stack in order to jump via it to he jmp_rip entry point.
         */
        pushl $GDT_SEL(APGDT_KCNORM_DESCR)
        pushl %cs:AP_OFFSET(jmp_rip)

        /*
         * If we won't update segment registers with a new selector value
         * we're risking to cautch a fault.
         */
        movw $GDT_SEL(APGDT_KCNORM_DESCR), %ax
        movw %ax, %ds
        movw %ax, %es
        movw %ax, %gs
        movw %ax, %fs
        movw %ax, %gs

        lretl
        
.code16
ap_boot_idt_descr:
        .word 0xffff
        .long 0x0

/* Start of struct ap_config */
ap_config:
jmp_rip:
        .long 0
page_addr:
        .long 0
ap_boot_gdt:
        .quad   0x0000000000000000  /* NULL descriptor */
        .quad   0x00cf9b000000ffff  /* 32bit Code segment */
        .quad   0x0000000000000000  /* 32bit Code segment */        
ap_boot_gdt_end:
boot_gdt_descr: 
        .word ap_boot_gdt_end - ap_boot_gdt
        .long AP_OFFSET(ap_boot_gdt)
/* end of struct ap_config */

.fill 64, 8, 0
ap_boot_stack:

.align 4
ap_boot_end: