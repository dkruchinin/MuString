#include <kernel/syscalls.h>
#include <eza/arch/page.h>

.global sighandler_invoker

	.align PAGE_SIZE

trampoline_sighandler_invoker:
	/* Prepare arguments for signal handler. */
	movq 0x8(%rsp),%rdi
	movq 0x10(%rsp),%rsi
	movq 0x18(%rsp),%rdx
	callq (%rsp)

	/* Restore context. */
	movq %rsp,%rdi
	movq $SC_SIGRETURN,%rax
	syscall

	