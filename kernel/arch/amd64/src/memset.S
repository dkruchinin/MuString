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
 * (c) Copyright newlib dev team - originally based on newlib amd64 port.
 * (c) Copyright 2006,2007,2008 MuiString Core Team <http://mstring.jarios.org>
 * (c) Copyright 2005,2008 MadTirra <madtirra@jarios.org>
 *
 * mstring/mach/amd64/memset.S: optimized memset
 *
 */
	
.global arch_memset

arch_memset:
	movq    %rdi, %r9 /* Save return value */
	movq    %rsi, %rax
	movq    %rdx, %rcx
	cmpq    $16, %rdx
	jb      byte_set

	movq    %rdi, %r8 /* Align on quad word boundary */
	andq    $7, %r8
	jz      quadword_aligned
	movq    $8, %rcx
	subq    %r8, %rcx
	subq    %rcx, %rdx
	rep     stosb
	movq    %rdx, %rcx

quadword_aligned:
	movabs  $0x0101010101010101, %r8 /* fullfill */
	movzbl  %sil, %eax
	imul    %r8, %rax
	cmpq    $256, %rdx
	jb      quadword_set

	shrq    $7, %rcx  /* Store 128 bytes at a time with minimum cache polution */

	.p2align 4
loop:
	movntiq %rax,     (%rdi) /* Maybe use xmm0 for this purpose ?*/
	movntiq %rax,   8 (%rdi)
	movntiq %rax,  16 (%rdi)
	movntiq %rax,  24 (%rdi)
	movntiq %rax,  32 (%rdi)
	movntiq %rax,  40 (%rdi)
	movntiq %rax,  48 (%rdi)
	movntiq %rax,  56 (%rdi)
	movntiq %rax,  64 (%rdi)
	movntiq %rax,  72 (%rdi)
	movntiq %rax,  80 (%rdi)
	movntiq %rax,  88 (%rdi)
	movntiq %rax,  96 (%rdi)
	movntiq %rax, 104 (%rdi)
	movntiq %rax, 112 (%rdi)
	movntiq %rax, 120 (%rdi)

	leaq    128 (%rdi), %rdi

	dec     %rcx
	jnz     loop

	sfence
	movq    %rdx, %rcx
	andq    $127, %rcx
	rep     stosb
	movq    %r9, %rax
	ret

byte_set:
	rep     stosb
	movq    %r9, %rax
	ret

quadword_set:
	shrq    $3, %rcx
	.p2align 4
	rep     stosq
	movq    %rdx, %rcx
	andq    $7, %rcx
	rep     stosb  /* Store the remaining bytes */
	movq    %r9, %rax
	ret

