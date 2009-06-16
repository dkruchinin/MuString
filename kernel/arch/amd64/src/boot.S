/*
 * This program is free software ; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation ; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY ; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program ; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * (c) Copyright 2006,2007,2008 MString Core Team <http://mstring.jarios.org>
 * (c) Copyright 2009 Dan Kruchinin <dan.kruchinin@gmail.com>
 *
 * Boot process for AMD64
 */
        

#include <config.h>
#include <arch/page.h>
#include <arch/boot.h>
#include <arch/pt_defs.h>
#include <arch/msr.h>
#include <arch/cpu.h>

.set KTEXT_SEG,   8
.set KDATA_SEG,   16
.set KTEXT32_SEG, 24

.section bootstrap_text, "ax"
.code32
.globl  start, _start, start32, start64, kernel_main, smp_start32
.align 4

multiboot_header:
        .long MULTIBOOT_HEADER_MAGIC
        .long MULTIBOOT_HEADER_FLAGS
        .long -(MULTIBOOT_HEADER_MAGIC + MULTIBOOT_HEADER_FLAGS)
        .long multiboot_header
        .long __bootstrap_start
        .long 0
        .long 0
        .long start

_start:
start:
        movl $kernel_jump_addr, %edx
        movl $KVIRT_TO_PHYS(kernel_main), 0(%edx)
        addl $(KERNEL_OFFSET & 0xFFFFFFFF), 0(%edx)
        movl $(KERNEL_OFFSET >> 32), 4(%edx)
start32:        
        cli /* make sure that interrupts are disabled */

        /* save multiboot info pointer */
        movl %ebx, KVIRT_TO_PHYS(multiboot_info_ptr)
        /* and multiboot magic value */
        movl %eax, KVIRT_TO_PHYS(multiboot_magic)

smp_start32:    
        movl $kernel_stack, %esp

        /* Check  */
        movl $0x80000000, %eax
        cpuid
        cmpl $0x80000000, %eax
        jbe no_long_mode

        movl $0x80000001, %eax
        cpuid
        btl $29, %edx
        jnc no_long_mode
        
        lgdt boot_gdt_descr

        movl $KDATA_SEG, %eax
        movl %eax, %ds
        movl %eax, %es
        movl %eax, %fs
        movl %eax, %gs

        /* Enable 64-bit page translation-table by setting CR4.PAE = 1 */
        movl %cr4, %eax
        orl $CR4_PAE, %eax
        movl %eax, %cr4

        /*
         * Create the long-mode page tables, and initialize the 64-bit CR3
         * to point to the base of PML4 containing 4M identity mapping and higher-half
         * kernel mapping.
         */
        movl $pml4_base, %eax
        movl %eax, %cr3

        /* Enable long mode (set EFER.LME = 1) */
        movl $MSR_EFER, %ecx
        rdmsr
        btsl $EFER_LME, %eax
        wrmsr

        /* Enable paging to activate long mode */
        movl %cr0, %eax
        orl $CR0_PG, %eax
        movl %eax, %cr0

        jmpl $KTEXT_SEG, $start64

.code64
start64:
        
        movq $PHYS_TO_KVIRT(kernel_stack), %rsp
        movq kernel_jump_addr, %rax
        callq *%rax
        cli
        hlt

.code32
no_long_mode:
        movl $no_long_mode_msg, %esi
	    movl $0xb8000, %edi			/* base of EGA text mode memory */
	    xorl %eax, %eax
	
	    movw $0x3d4, %dx			/* read bits 8 - 15 of the cursor address */
	    movb $0xe, %al
	    outb %al, %dx
	
	    movw $0x3d5, %dx
	    inb %dx, %al
	    shl $8, %ax
	
	    movw $0x3d4, %dx			/* read bits 0 - 7 of the cursor address */

	    movb $0xf, %al
	    outb %al, %dx
	
	    movw $0x3d5, %dx
	    inb %dx, %al
	
	    cmp $1920, %ax
	    jbe cursor_ok
		movw $1920, %ax			/* sanity check for the cursor on the last line */
cursor_ok:	
	    movw %ax, %bx
        
	    shl $1, %eax
	    addl %eax, %edi
	
	    movw $0x0c00, %ax			/* black background, light red foreground */
	    cld	
ploop:
		lodsb
		cmp $0, %al
		je ploop_end
		stosw
		inc %bx	
		jmp ploop
ploop_end:	
	    movw $0x3d4, %dx			/* write bits 8 - 15 of the cursor address */
	    movb $0xe, %al
	    outb %al, %dx
	
	    movw $0x3d5, %dx
	    movb %bh, %al
	    outb %al, %dx
	
	    movw $0x3d4, %dx			/* write bits 0 - 7 of the cursor address */
	    movb $0xf, %al
	    outb %al, %dx
	
	    movw $0x3d5, %dx
	    movb %bl, %al
	    outb %al, %dx

        /* and finally spin forever */
1:
        jmp 1b
        
.section bootstrap_data, "aw"

.globl kernel_jump_addr
kernel_jump_addr:
        .quad 0
        
boot_gdt_descr:
        .word boot_gdt_end - boot_gdt_table
        .long boot_gdt_table

boot_gdt_table:
        .quad   0x0000000000000000  /* NULL descriptor */        
        .quad   0x00af9a000000ffff  /* Code segment */
        .quad   0x008f92000000ffff  /* Data segment */
        .quad   0x00cf9a000000ffff  /* 32bit Code segment */
boot_gdt_end:

.align 4096
.global pd_base
pd_base:
        .quad (PDE_PRESENT | PDE_RW | PDE_PS)
        .quad 0x200000 + (PDE_PRESENT | PDE_RW | PDE_PS)
        .fill 510, 8, 0

.align 4096
.global pdp_base
pdp_base:
        .quad pd_base + (PDE_PRESENT | PDE_RW)
        .fill 509, 8, 0
        .quad pd_base + (PDE_PRESENT | PDE_RW)
        .quad 0x0000000000000000

.align 4096
.global pml4_base
pml4_base:
        .quad pdp_base + (PDE_PRESENT | PDE_RW)
        .fill 510, 8, 0        
        .quad pdp_base + (PDE_PRESENT | PDE_RW)
        
/* 4K bootstrap kernel stack */
.fill 512, 8, 0
.global kernel_stack
kernel_stack:
no_long_mode_msg:
        .asciz "FATAL ERROR: CPU doesn't support 64-bit long mode!"