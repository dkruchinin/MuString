#include <eza/arch/context.h>
#include <eza/arch/page.h>
#include <eza/arch/current.h>

.code64
.text

.global smp_local_timer_interrupt_handler
.global smp_scheduler_interrupt_handler

smp_local_timer_interrupt_handler:
	ENTER_INTERRUPT_CTX(l1,0)
	callq apic_timer_hack
	callq smp_local_timer_interrupt_tick
lapic_int_finish:
	callq local_apic_send_eoi
	COMMON_INTERRUPT_EXIT_PATH

smp_scheduler_interrupt_handler:
	ENTER_INTERRUPT_CTX(l2,0)
	SAVE_ALL
	call do_smp_scheduler_interrupt_handler
	jmp lapic_int_finish
