#include <eza/arch/context.h>
#include <eza/arch/page.h>
#include <eza/arch/cpu.h>
#include <eza/arch/current.h>

.code64
.text

.global smp_local_timer_interrupt_handler
.global smp_scheduler_interrupt_handler

smp_local_timer_interrupt_handler:
	push %rax 
	ENTER_INTERRUPT_CTX(l1,INT_STACK_EXTRA_PUSHES)
	callq apic_timer_hack
	callq smp_local_timer_interrupt_tick
lapic_int_finish:
	callq local_apic_send_eoi
	COMMON_INTERRUPT_EXIT_PATH

smp_scheduler_interrupt_handler:
	push %rax
	ENTER_INTERRUPT_CTX(l2,INT_STACK_EXTRA_PUSHES)
	call do_smp_scheduler_interrupt_handler
	jmp lapic_int_finish
