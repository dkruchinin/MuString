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
 * (c) Copyright 2005,2008 Tirra <madtirra@jarios.org>
 *
 * mlibc/mach/amd64/memcpy.S: optimized memcpy for amd64
 *
 */
	
	
.global arch_memcpy

arch_memcpy:
	  movq    %rdi, %rax                /* Store destination in return value */
	  cmpq    $16, %rdx
	  jb      byte_copy

	  movq    %rdi, %r8                 /* Align destination on quad word boundary */
	  andq    $7, %r8
	  jz      quadword_aligned
	  movq    $8, %rcx
	  subq    %r8, %rcx
	  subq    %rcx, %rdx
	  rep     movsb

quadword_aligned:
	  cmpq    $256, %rdx
	  jb      quadword_copy

	  movq    %rax, -8  (%rsp)
	  movq    %r12, -16 (%rsp)
	  movq    %r13, -24 (%rsp)
	  movq    %r14, -32 (%rsp)

	  movq    %rdx, %rcx                /* Copy 128 bytes at a time with minimum cache polution */
	  shrq    $7, %rcx

	  .p2align 4
loop:
	  prefetchnta   768 (%rsi)
	  prefetchnta   832 (%rsi)

	  movq       (%rsi), %rax
	  movq     8 (%rsi), %r8
	  movq    16 (%rsi), %r9
	  movq    24 (%rsi), %r10
	  movq    32 (%rsi), %r11
	  movq    40 (%rsi), %r12
	  movq    48 (%rsi), %r13
	  movq    56 (%rsi), %r14

	  movntiq %rax,    (%rdi)
	  movntiq %r8 ,  8 (%rdi)
	  movntiq %r9 , 16 (%rdi)
	  movntiq %r10, 24 (%rdi)
	  movntiq %r11, 32 (%rdi)
	  movntiq %r12, 40 (%rdi)
	  movntiq %r13, 48 (%rdi)
	  movntiq %r14, 56 (%rdi)

	  movq     64 (%rsi), %rax
	  movq     72 (%rsi), %r8
	  movq     80 (%rsi), %r9
	  movq     88 (%rsi), %r10
	  movq     96 (%rsi), %r11
	  movq    104 (%rsi), %r12
	  movq    112 (%rsi), %r13
	  movq    120 (%rsi), %r14

	  movntiq %rax,  64 (%rdi)
	  movntiq %r8 ,  72 (%rdi)
	  movntiq %r9 ,  80 (%rdi)
	  movntiq %r10,  88 (%rdi)
	  movntiq %r11,  96 (%rdi)
	  movntiq %r12, 104 (%rdi)
	  movntiq %r13, 112 (%rdi)
	  movntiq %r14, 120 (%rdi)

	  leaq    128 (%rsi), %rsi
	  leaq    128 (%rdi), %rdi

	  dec     %rcx
	  jnz     loop

	  sfence
	  movq    %rdx, %rcx
	  andq    $127, %rcx
	  rep     movsb
	  movq    -8  (%rsp), %rax
	  movq    -16 (%rsp), %r12
	  movq    -24 (%rsp), %r13
	  movq    -32 (%rsp), %r14
	  ret


byte_copy:
	  movq    %rdx, %rcx
	  rep     movsb
	  ret


quadword_copy:
	  movq    %rdx, %rcx
	  shrq    $3, %rcx
	  .p2align 4
	  rep     movsq
	  movq    %rdx, %rcx
	  andq    $7, %rcx
	  rep     movsb                   /* Copy the remaining bytes */
	  ret

