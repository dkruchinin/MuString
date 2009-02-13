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
 * eza/arch/amd64/utrampoline.S: AMD64-specific code for userspace
 *                               trampolines.
 */

#include <kernel/syscalls.h>
#include <eza/arch/page.h>

.global __userspace_trampoline_codepage
.global trampoline_cancellation_invoker
.global trampoline_sighandler_invoker_int
.align PAGE_SIZE

__userspace_trampoline_codepage:
trampoline_sighandler_invoker_int:
	/* Prepare arguments for signal handler. */
	movq 0x8(%rsp),%rdi
	movq 0x10(%rsp),%rsi
	movq 0x18(%rsp),%rdx
	callq (%rsp)

	/* Restore context. */
	movq %rsp,%rdi
	movq $SC_SIGRETURN,%rax
	syscall

	/* We shouldn't return from this syscall here !
	 * So insert some preventive logic which means termination
	 * of the calling thread.
	 */
	movq $SC_THREAD_EXIT,%rax
	movq $-1,%rdi
	syscall

trampoline_cancellation_invoker:
	/* Invoke userspace cancellation handler. */
	callq %rdi

	/* Force thread termination with 'PTHREAD_CANCELLED' exit code. */
	movq $SC_THREAD_EXIT,%rax
	movq $-1,%rdi
	syscall
