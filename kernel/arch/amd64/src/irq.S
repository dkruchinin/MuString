#include <arch/seg.h>
#include <arch/context.h>

.code64
.text

.macro GEN_INTRS_TABLE idx, items, entry_point
.set cnt, \idx
.rept \items - \idx
        .align 16
        pushq %rax
        movq $cnt, %rax
        jmp \entry_point
.set cnt, cnt+1
.endr
.endm

.globl __faults_table
.globl __do_handle_fault
fault_entry_point:
        cmpq $GDT_SEL(KCODE_DESCR), INT_STACK_EXTRA_PUSHES + INT_STACK_FRAME_CS_OFFT(%rsp)
        je 1f
        swapgs
1:      
        SAVE_GPRS
        movq %rsp, %rdi
        movq %rax, %rsi
        callq __do_handle_fault
        RESTORE_GPRS
        popq %rax
        sti
        iretq

.globl __irqs_table
.globl __do_handle_irq
irq_entry_point:
        cmpq $GDT_SEL(KCODE_DESCR), INT_STACK_EXTRA_PUSHES + INT_STACK_FRAME_CS_OFFT(%rsp)
        je 1f
        swapgs
1:      
        SAVE_GPRS
        subq $32, %rax
        movq %rax, %rdi
        callq __do_handle_irq
        RESTORE_GPRS
        popq %rax
        sti
        iretq

.align 16
__faults_table:
        GEN_INTRS_TABLE 0, 32, fault_entry_point

.align 16
__irqs_table:
         GEN_INTRS_TABLE 32, 255, irq_entry_point