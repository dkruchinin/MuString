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
 * (c) Copyright 2008 Michael Tsymbalyuk <mtzaurus@gmail.com>
 *
 * /eza/amd64/syscalls.S: low level syscall-related, AMD64-specific
 *                        functionality
 */

#include <eza/arch/context.h>
#include <eza/errno.h>

.global syscall_point

/* NOTE: 'syscall_point' must immediately follow the list
 * of syscalls since it is used for determening the amount
 * of available system calls.
 * NOTE: since %rcx is used by the 'syscall' opcode for saving
 * return address, processes that invoke 'syscall' must pass
 * the fourth parameter via %r10.
 */
syscall_table:
	.quad	sys_get_pid       		/* 0 */
	.quad 	sys_create_task
	.quad 	sys_task_control
	.quad   sys_mmap
	.quad   sys_create_port
	.quad   sys_port_send           	/* 5 */
	.quad   sys_port_receive
	.quad   sys_port_reply
	.quad   sys_allocate_ioports
	.quad   sys_free_ioports
	.quad   sys_create_irq_counter_array	/* 10 */
	.quad   sys_wait_on_irq_array
        .quad   sys_ipc_port_poll
	.quad   sys_nanosleep
	.quad   sys_scheduler_control
	.quad   sys_get_tid                     /* 15  */

/* Don't change the order of these macros. */
#define NUM_OF_SYSCALLS \
	(syscall_point-syscall_table)/8

syscall_point:
	/* Local interrupts are disabled upon entering the entrypoint.
	 */
	cmp $NUM_OF_SYSCALLS,%rax
	jbe good_syscall_number

	movq $-ENOSYS,%rax
        sysretq

good_syscall_number:
	swapgs
	movq %rsp,%gs:CPU_SCHED_STAT_USTACK_OFFT
	movq %gs:CPU_SCHED_STAT_KSTACK_OFFT,%rsp

	/* After changing stack pointer, we can enable IRQs */
	sti
	SAVE_GPR

	/* Since %rcx contains the return address, we should reload
	 * %rcx from %r10.
	 */
	movq %r10,%rcx

	call *syscall_table(,%rax,8)
	jmp return_from_syscall
