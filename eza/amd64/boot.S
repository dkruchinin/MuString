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
 * /eza/amd64/boot.S: boostrap for amd64 with base init and long mode 
 *                    switching
 *
 */


#include <config.h>
#include <eza/arch/boot.h>
#include <eza/arch/e820map.h>
#include <eza/arch/page.h>
#include <eza/arch/cpu.h>
	
#define START_STACK	(BOOT_OFFSET - BOOT_STACK_SIZE)
	
.section K_TEXT_START, "ax"

.code32
.align 4
.global multiboot_image_start
multiboot_header:
	.long MULTIBOOT_HEADER_MAGIC
	.long MULTIBOOT_HEADER_FLAGS
	.long -(MULTIBOOT_HEADER_MAGIC + MULTIBOOT_HEADER_FLAGS) /* checksum */
	.long multiboot_header
	.long unmapped_ktext_start
	.long 0
	.long 0
	.long multiboot_image_start /* start point */

multiboot_image_start:
	movl $START_STACK, %esp			/* initialize stack pointer */
	lgdtl bootstrap_gdtr			/* initialize Global Descriptor Table register */

	movw $gdtselector(KDATA_DES), %cx
	movw %cx, %es
	movw %cx, %ds				/* kernel data + stack */
	movw %cx, %ss
	/* Simics seems to remove hidden part of GS on entering user mode
	 * when _visible_ part of GS does not point to user-mode segment */
	movw $gdtselector(UDATA_DES), %cx
	movw %cx, %fs
	movw %cx, %gs
	
	jmpl $gdtselector(KTEXT32_DES), $multiboot_meeting_point
	multiboot_meeting_point:
	
	movl %eax, grub_eax			/* save parameters from GRUB */
	movl %ebx, grub_ebx
	
	/* Protected 32-bit. We want to reuse the code-seg descriptor,
	 * the Default operand size must not be 1 when entering long mode */
	
	movl $(INTEL_CPUID_EXTENDED), %eax  /* now we need to check varios cpu features*/
 	cpuid
 	cmp $(INTEL_CPUID_EXTENDED), %eax
	ja extended_cpuid_supported
		
		movl $extended_cpuid_msg, %esi
		jmp error_halt
	
	extended_cpuid_supported:
	
	movl $(AMD_CPUID_EXTENDED), %eax
	cpuid
	bt $(AMD_EXT_LONG_MODE), %edx
 	jc long_mode_supported
		
		movl $long_mode_msg, %esi
		jmp error_halt

	long_mode_supported:
	
	bt $(AMD_EXT_NOEXECUTE), %edx
	jc noexecute_supported
	
		movl $noexecute_msg, %esi
		jmp error_halt
	
	noexecute_supported:
	
	movl $(INTEL_CPUID_STANDARD), %eax
	cpuid
	bt $(INTEL_FXSAVE), %edx
 	jc fx_supported
	
		movl $fx_msg, %esi
		jmp error_halt
	
	fx_supported:
	
	bt $(INTEL_SSE2), %edx
 	jc sse2_supported
	
		movl $sse2_msg, %esi
		jmp error_halt
	
	sse2_supported:
		
	/* Enable 64-bit page translation entries - CR4.PAE = 1.
	 * Paging is not enabled until after long mode is enabled */
	
	movl %cr4, %eax
	btsl $5, %eax
	movl %eax, %cr4

	/* Set up paging tables */
	
	leal ptl_0, %eax
	movl %eax, %cr3
	
	/* Enable long mode */
	
	movl $EFER_MSR_NUM, %ecx			/* EFER MSR number */
	rdmsr						/* Read EFER */
	btsl $AMD_LME_FLAG, %eax			/* Set LME = 1 */
	wrmsr						/* Write EFER */
	
	/* Enable paging to activate long mode (set CR0.PG = 1) */
	
	movl %cr0, %eax
	btsl $31, %eax
	movl %eax, %cr0
	
	/* At this point we are in compatibility mode */
	
	jmpl $gdtselector(KTEXT_DES), $start64

.code64
start64:
	movq $(p2k(START_STACK)), %rsp
	movl grub_eax, %eax
	movl grub_ebx, %ebx
	
	cmpl $MULTIBOOT_LOADER_MAGIC, %eax		/* compare GRUB signature */
	je valid_boot
		
		xorl %ecx, %ecx				/* no memory size or map available */
		movl %ecx, e820count
		
		jmp invalid_boot
		
	valid_boot:
		
		movl (%ebx), %eax			/* ebx = physical address of struct multiboot_info */
		
		bt $3, %eax				/* mbi->flags[3] (mods_count, mods_addr valid) */
		jc mods_valid
			
			xorq %rcx, %rcx
			movq %rcx, init
			jmp mods_end
		
		mods_valid:
		
		xorq %rcx, %rcx
		movl 20(%ebx), %ecx			/* mbi->mods_count */
		movq %rcx, init
		
		cmpl $0, %ecx
		je mods_end
		
		movl 24(%ebx), %esi			/* mbi->mods_addr */
		movq $init, %rdi
		
		mods_loop:
			
			xorq %rdx, %rdx
			movl 0(%esi), %edx		/* mods->mod_start */
			/*movq $0xffff800000000000, %r10*/
			addq %r10, %rdx
			movq %rdx, 8(%rdi)
			
			xorq %rdx, %rdx
			movl 4(%esi), %edx
			subl 0(%esi), %edx		/* mods->mod_end - mods->mod_start */
			movq %rdx, 16(%rdi)
			
			addl $16, %esi
			addq $16, %rdi
			
			loop mods_loop
			
		mods_end:
		bt $6, %eax				/* mbi->flags[6] (mmap_length, mmap_addr valid)	*/
		jc mmap_valid
			
			xorl %edx, %edx
			jmp mmap_invalid
			
		mmap_valid:
		movl 44(%ebx), %ecx			/* mbi->mmap_length */
		movl 48(%ebx), %esi			/* mbi->mmap_addr */
		movq $e820table, %rdi
		xorl %edx, %edx
		
		mmap_loop:
			cmpl $0, %ecx
			jle mmap_end
			
			movl 4(%esi), %eax		/* mmap->base_addr_low */
			movl %eax, (%rdi)
			
			movl 8(%esi), %eax		/* mmap->base_addr_high */
			movl %eax, 4(%rdi)
			
			movl 12(%esi), %eax		/* mmap->length_low */
			movl %eax, 8(%rdi)
			
			movl 16(%esi), %eax		/* mmap->length_high */
			movl %eax, 12(%rdi)
			
			movl 20(%esi), %eax		/* mmap->type */
			movl %eax, 16(%rdi)
			
			movl (%esi), %eax		/* mmap->size */
			addl $0x4, %eax
			addl %eax, %esi
			subl %eax, %ecx
			addq $E820MAP_E820_RECORDSIZE, %rdi
			incl %edx
			jmp mmap_loop
		
		mmap_end:
		
		mmap_invalid:
		movl %edx, e820count
		
	invalid_boot:
#ifdef CONFIG_SMP
	/* we're need to copy all bootstrap routines below 1024k */
	movq $BOOT_OFFSET, %rsi
	movq $AP_BOOT_OFFSET, %rdi
	movq $_hardcoded_unmapped_size, %rcx
	cld
	rep movsb

#endif

	call main_routine   /* call normal C code,uff */
	
	cli
	hlt


/* Print string from %esi to EGA display (in red) and halt */
error_halt:
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
		
	cli
	hlt
				

.section K_INI_PTLS, "aw", @progbits

/*
 * Macro for generating initial page table contents.
 * cnt	Number of entries to generate. Must be multiple of 8.
 * g	Number of GB that will be added to the mapping.
 */
.macro ptl2gen cnt g 
.if \cnt
	ptl2gen "\cnt - 8" \g 
	.quad ((\cnt - 8) * 0x200000) + (\g * 1024 * 1024 * 1024) | (PTL_WRITABLE | PTL_PRESENT | PTL_2MB_PAGE)
	.quad ((\cnt - 7) * 0x200000) + (\g * 1024 * 1024 * 1024) | (PTL_WRITABLE | PTL_PRESENT | PTL_2MB_PAGE)
	.quad ((\cnt - 6) * 0x200000) + (\g * 1024 * 1024 * 1024) | (PTL_WRITABLE | PTL_PRESENT | PTL_2MB_PAGE)
	.quad ((\cnt - 5) * 0x200000) + (\g * 1024 * 1024 * 1024) | (PTL_WRITABLE | PTL_PRESENT | PTL_2MB_PAGE)
	.quad ((\cnt - 4) * 0x200000) + (\g * 1024 * 1024 * 1024) | (PTL_WRITABLE | PTL_PRESENT | PTL_2MB_PAGE)
	.quad ((\cnt - 3) * 0x200000) + (\g * 1024 * 1024 * 1024) | (PTL_WRITABLE | PTL_PRESENT | PTL_2MB_PAGE)
	.quad ((\cnt - 2) * 0x200000) + (\g * 1024 * 1024 * 1024) | (PTL_WRITABLE | PTL_PRESENT | PTL_2MB_PAGE)
	.quad ((\cnt - 1) * 0x200000) + (\g * 1024 * 1024 * 1024) | (PTL_WRITABLE | PTL_PRESENT | PTL_2MB_PAGE)
.endif
.endm

/* Page table for pages in the first gigabyte. */
.align 4096
.global ptl_2_0g
ptl_2_0g:	
	ptl2gen 512 0

/* Page table for pages in the second gigabyte. */
.align 4096
.global ptl_2_1g
ptl_2_1g:
	ptl2gen 512 1

/* Page table for pages in the third gigabyte. */
.align 4096
.global ptl_2_2g
ptl_2_2g:
	ptl2gen 512 2

/* Page table for pages in the fourth gigabyte. */
.align 4096
.global ptl_2_3g
ptl_2_3g:
	ptl2gen 512 3

.align 4096
.global ptl_1
ptl_1:
	/* Identity mapping for [0; 4G) */
	.quad ptl_2_0g + (PTL_WRITABLE | PTL_PRESENT)
	.quad ptl_2_1g + (PTL_WRITABLE | PTL_PRESENT) 
	.quad ptl_2_2g + (PTL_WRITABLE | PTL_PRESENT)
	.quad ptl_2_3g + (PTL_WRITABLE | PTL_PRESENT)
	.fill 506, 8, 0
	/* Mapping of [0; 1G) at -2G */
	.quad ptl_2_0g + (PTL_WRITABLE | PTL_PRESENT)
	.fill 1, 8, 0
	
.align 4096
.global ptl_0
ptl_0:
	.quad ptl_1 + (PTL_WRITABLE | PTL_PRESENT)
	.fill 255,8,0
	.quad ptl_1 + (PTL_WRITABLE | PTL_PRESENT)
	.fill 254,8,0
	.quad ptl_1 + (PTL_WRITABLE | PTL_PRESENT)

.section K_DATA_START, "aw", @progbits

.global bootstrap_gdtr
bootstrap_gdtr:
	.word gdtselector(GDT_ITEMS)
	.long k2p(gdt)

grub_eax:
	.long 0

grub_ebx:
	.long 0

extended_cpuid_msg:
	.asciz "Extended CPUID not supported. System halted."
long_mode_msg:
	.asciz "64 bit long mode not supported. System halted."
noexecute_msg:
	.asciz "No-execute pages not supported. System halted."
fx_msg:
	.asciz "FXSAVE/FXRESTORE instructions not supported. System halted."
sse2_msg:
	.asciz "SSE2 instructions not supported. System halted."
