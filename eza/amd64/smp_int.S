#include <eza/arch/context.h>
#include <eza/arch/page.h>
#include <eza/arch/current.h>

.code64
.text

.global smp_local_timer_interrupt_handler
.global smp_scheduler_interrupt_handler

smp_local_timer_interrupt_handler:
	SAVE_ALL
	sti
	call smp_local_timer_interrupt_tick
	call local_apic_send_eoi
	RESTORE_ALL
	iretq

smp_scheduler_interrupt_handler:
	SAVE_ALL
	sti
	call do_smp_scheduler_interrupt_handler
	call local_apic_send_eoi
	RESTORE_ALL
	iret
