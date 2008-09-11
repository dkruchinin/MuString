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
 * (c) Copyright 2006,2007,2008 MString Core Team <http://mstring.berlios.de>
 * (c) Copyright 2005,2008 Tirra <tirra.newly@gmail.com>
 *
 * /eza/amd64/smpap.S: low level assembler for initializate
 *                     processor and ap on smp mode
 *
 */

#include <eza/arch/boot.h>
#include <eza/arch/cpu.h>
#include <eza/arch/page.h>
#include <eza/arch/e820map.h>

.section K_TEXT_START, "ax"
	
#ifdef CONFIG_SMP

.global unmapped_ap_boot

.align 4096
unmapped_ap_boot:
.code16
	cli
	xorw %ax, %ax
	movw %ax, %ds
	/* initialize GDT */
	lgdtl ap_gdtr
	movl %cr0, %eax
	orl $1, %eax
	/* switching to protected mode */
	movl %eax, %cr0
	jmpl $gdtselector(KTEXT32_DES), $jump_to_kernel-BOOT_OFFSET+AP_BOOT_OFFSET

jump_to_kernel:
.code32
	movw $gdtselector(KDATA_DES), %ax
	movw %ax, %ds
	movw %ax, %es
	movw %ax, %ss
	movw $gdtselector(UDATA_DES), %ax
	/* enable 64bit page translations */
	movl %cr4, %eax
	btsl $5, %eax
	movl %eax, %cr4
	/* setup paging tables */
	leal ptl_0, %eax
	movl %eax, %cr3
	/* enable long mode */
	movl $EFER_MSR_NUM, %ecx
	rdmsr
	btsl $AMD_LME_FLAG, %eax
	wrmsr
	/* enabling paging will activate long mode */
	movl %cr0, %eax
	btsl $31, %eax
	movl %eax, %cr0
	/* ufgh, we're in compatibility mode now */
	jmpl $gdtselector(KTEXT_DES), $start64-BOOT_OFFSET+AP_BOOT_OFFSET
.code64
start64:
	/* here we're go - call C code */
	movq $ap_stack+2048, %rsp
	call main_smpap_routine-AP_BOOT_OFFSET+BOOT_OFFSET
	/* will never returns */
	
#endif /* CONFIG_SMP */
	
.section K_DATA_START, "aw", @progbits
	
#ifdef CONFIG_SMP

.global ap_stack
ap_stack:
	.rept 1024
	.quad 0
	.endr
	
.global unmapped_ap_gdtr
unmapped_ap_gdtr:
	.word 0
	.long 0
	
#endif /* CONFIG_SMP */

