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
 * (c) Copyright 2008 Tirra <tirra.newly@gmail.com>
 * (c) Copyright 2008 Michael Tsymbalyuk <mtzaurus@gmail.com>
 *
 * eza/amd64/sched/context.S: architecture specific context_[save|restore]
 * 			      functions
 *
 */

.text

.global arch_context_save
.global arch_context_restore
.global arch_hw_activate_task

#include <eza/arch/context.h>
	
arch_context_save:
	movq (%rsp), %rdx

	CONTEXT_SAVE_ARCH_CORE %rdi %rdx
	xorq %rax, %rax
	incq %rax

	ret

arch_context_restore: 
	CONTEXT_RESTORE_ARCH_CORE %rdi %rdx

	movq %rdx,(%rsp)
	xorq %rax,%rax

	ret

/* 
 * %rdi: contains stack pointer of the task being activated. 
 * %rsi: contains TLB4 value (cr3) of the task being activated. 
 */
arch_hw_activate_task:
  pushq %rax
  SAVE_ALL

  pushq $__return_from_sleep
  movq %rsi, %rax
  movq %rax, %cr3
  mov %rdi, %rsp
  retq

__return_from_sleep:
  RESTORE_ALL
  popq %rax
  retq

