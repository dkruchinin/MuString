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
 * (c) Copyright 2010 Jari OS non-profit org <http://jarios.org>
 *
 * /mstring/amd64/syscalls.S: low level syscall-related, AMD64-specific
 *                        functionality
 */

#include <arch/context.h>
#include <arch/seg.h>
#include <mstring/errno.h>
#include <config.h>

.global syscall_point

/* NOTE: 'syscall_point' must immediately follow the list
 * of syscalls since it is used for determening the amount
 * of available system calls.
 * NOTE: since %rcx is used by the 'syscall' opcode for saving
 * return address, processes that invoke 'syscall' must pass
 * the fourth parameter via %r10.
 */
syscall_table:
	.quad 	sys_create_task             /* 0 */
	.quad 	sys_task_control
	.quad   sys_mmap
	.quad   sys_create_port
	.quad   sys_port_receive
	.quad   sys_allocate_ioports        /* 5 */
	.quad	  sys_free_ioports
	.quad   sys_create_irq_counter_array
	.quad   sys_wait_on_irq_array
	.quad   sys_register_free_irq
  .quad   sys_unregister_irq          /* 10 */
  .quad   sys_ipc_port_poll
	.quad   sys_nanosleep
	.quad   sys_scheduler_control
	.quad   sys_exit
	.quad   sys_open_channel            /* 15 */
	.quad   sys_close_channel
	.quad   sys_close_port
	.quad   sys_control_channel
	.quad   sys_sync_create_object
	.quad   sys_sync_control            /* 20 */
	.quad   sys_sync_destroy
	.quad   sys_kill
	.quad   sys_signal
	.quad   sys_sigreturn
	.quad   sys_port_send_iov_v         /* 25 */
	.quad   sys_port_reply_iov
	.quad   sys_sigaction
	.quad   sys_thread_kill
	.quad   sys_sigprocmask
	.quad   sys_thread_exit             /* 30 */
	.quad   sys_timer_create
	.quad   sys_timer_control
	.quad   sys_munmap
	.quad   sys_thread_wait
	.quad   sys_port_msg_read           /* 35 */
	.quad   sys_kernel_control
	.quad   sys_timer_delete
	.quad   sys_sigwaitinfo
	.quad   sys_sched_yield
	.quad   sys_memobj_create           /* 40 */
	.quad   sys_fork
	.quad   sys_grant_pages
	.quad   sys_waitpid
  .quad   sys_alloc_dma_pages
  .quad   sys_free_dma_pages          /* 45 */
	.quad   sys_port_control
	.quad   sys_port_msg_write
	.quad   sys_ptrace
	.quad   sys_chg_create_namespace
	.quad   sys_control_namespace
	.quad	sys_msync

/* Don't change the order of these macros. */
#define NUM_OF_SYSCALLS \
	(syscall_point-syscall_table)/8

syscall_point:
	/* Local interrupts are disabled upon entering the entrypoint.
	 */
	swapgs
	movq %rsp,%gs:CPU_SCHED_STAT_USTACK_OFFT
	movq %gs:CPU_SCHED_STAT_KSTACK_OFFT,%rsp
	SAVE_AND_LOAD_SEGMENT_REGISTERS

	/* Since we use IRET instead of SYSRETURN to return from syscall,
	 *  we need to setup a fake interrupt stack frame on the stack.
	 */
	subq $HW_INTERRUPT_CTX_SIZE,%rsp
	movq %rcx,HW_INTERRUPT_CTX_RIP_OFFT(%rsp)
	movq $(GDT_SEL(UCODE_DESCR) | SEG_DPL_USER),HW_INTERRUPT_CTX_CS_OFFT(%rsp)

	movq $(GDT_SEL(UDATA_DESCR) | SEG_DPL_USER),HW_INTERRUPT_CTX_SS_OFFT(%rsp)
	movq %gs:CPU_SCHED_STAT_USTACK_OFFT,%rcx
	movq %rcx,HW_INTERRUPT_CTX_RSP_OFFT(%rsp)

	/* After changing stack pointer, we can enable IRQs */
	sti
    pushq %rax
	SAVE_GPRS

#ifdef CONFIG_TRACE_SYSCALL_ACTIVITY
    pushq %rax
	pushq %rax /* Extra save of syscall number. */
	SAVE_GPRS
	mov %rax,%rdi
	call trace_sysenter
	RESTORE_GPRS
	popq %rax
#endif

	cmp $NUM_OF_SYSCALLS,%rax
	jb good_syscall_number

	movq $-ENOSYS,%rax
	jmp syscall_done

good_syscall_number:
	/* Since %rcx contains the return address, we should reload
	 * %rcx from %r10.
	 */
	movq %r10,%rcx
	call *syscall_table(,%rax,8)

syscall_done:
#ifdef CONFIG_TRACE_SYSCALL_ACTIVITY
	pop %rdi /* Restore syscal number saved earlier */
	push %rax
	mov %rax,%rsi
	call trace_sysreturn
	pop %rax
#endif

return_from_syscall:
	  /* Before returning from syscall we should check for pending works.
	   * Note that %rax contains return code.
	   */
    mov %gs:CPU_SCHED_STAT_USER_WORKS_OFFT, %rbx

	  cmpq $0, (%rbx)
	  je no_works_after_syscall

    pushq %rax
	  movq $__SYCALL_UWORK, %rdi
	  movq %rax, %rsi
	  movq %rsp, %rdx
    addq $8, %rdx
	  callq handle_uworks
    popq %rax

no_works_after_syscall:
	  RESTORE_GPRS

	  /* Now restore user %gs */
	  cli
	  addq $8,%rsp /* Simulate our extra 'push %rax' */
	  RESTORE_USER_SEGMENT_REGISTERS
    pushq %rax
	  movl %gs:(CPU_SCHED_STAT_USER_GS_OFFT), %eax
	  swapgs
	  movl %eax, %gs
    popq %rax
    orq $(1 << 9),HW_INTERRUPT_CTX_RFLAGS_OFFT(%rsp)
    iretq
