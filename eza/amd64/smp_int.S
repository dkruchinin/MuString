#include <eza/arch/context.h>
#include <eza/arch/page.h>
#include <eza/arch/current.h>

#define ENTER_INTERRUPT_CTX(label,extra_pushes) \
	cmp $gdtselector(KTEXT_DES),extra_pushes+INT_STACK_FRAME_CS_OFFT(%rsp) ;\
	je label; \
        swapgs ;\
label:	;\
	incq %gs:CPU_SCHED_STAT_IRQCNT_OFFT ;\
	push %rax ;\
	SAVE_ALL ;\
	sti

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
	jmp return_from_common_interrupt

smp_scheduler_interrupt_handler:
	ENTER_INTERRUPT_CTX(l2,0)
	SAVE_ALL
	call do_smp_scheduler_interrupt_handler
	jmp lapic_int_finish
