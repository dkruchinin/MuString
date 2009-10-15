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
 * (c) Copytight 2009 Dan Kruchinin <dk@jarios.org>         
 *
 */


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
        btq %rax, faults_with_errcode
        jc 1f

        subq $8, %rsp
        pushq %rbx
        movq 24(%rsp), %rbx
        movq %rbx, 16(%rsp)
        movq $0, 24(%rsp)
        popq %rbx
        
1:
        cmpq $GDT_SEL(KCODE_DESCR), 16 + INT_STACK_FRAME_CS_OFFT(%rsp)
        je 2f

        swapgs
2:
        sti
        SAVE_ALL
        movq %r12, %rdi
        movq %rax, %rsi        
        callq __do_handle_fault        
        
        movq (%rsp), %rbx   
        cmpq $GDT_SEL(KCODE_DESCR), INT_FULL_OFFSET_TO_CS(%rbx)
	    je 3f

	    /* Process deferred userspace-related works */
	    movq $__XCPT_ERR_UWORK, %rdi
	    xorq %rsi, %rsi
	    movq %rsp, %rdx
	    call handle_uworks

3:      
        RESTORE_ALL
        cli
        cmpq $GDT_SEL(KCODE_DESCR), 16 + INT_STACK_FRAME_CS_OFFT(%rsp)
        je 4f
        
        RESTORE_USER_SEGMENT_REGISTERS
        swapgs
4:
        popq %rax        
        addq $8, %rsp
        orq $(1 << 9),HW_INTERRUPT_CTX_RFLAGS_OFFT(%rsp)
        iretq
        
.globl __irqs_table
.globl __do_handle_irq
irq_entry_point:
        cmpq $GDT_SEL(KCODE_DESCR), 8 + INT_STACK_FRAME_CS_OFFT(%rsp)
        je 1f

        swapgs
        SAVE_AND_LOAD_SEGMENT_REGISTERS

1:      
        incq %gs:CPU_SCHED_STAT_IRQCNT_OFFT
        SAVE_ALL
        subq $32, %rax
        movq %rax, %rdi
        sti
        callq __do_handle_irq
        cli

return_from_irq:
	  /*
	   * Zero on top of the stack tells us that we haven't performed
	   * userspace-related checks for current task.
	   */
        pushq $0
        decq %gs:CPU_SCHED_STAT_IRQCNT_OFFT
        jnz no_preemption_in_interrupt

process_irq_deffered_actions:
        bt $CPU_SCHED_DEF_WORKS_F_IDX, %gs:CPU_SCHED_STAT_FLAGS_OFFT
        jnc no_irq_deffered_works
        btr $CPU_SCHED_DEF_WORKS_F_IDX, %gs:CPU_SCHED_STAT_FLAGS_OFFT
        addq $1, %gs:CPU_SCHED_STAT_PREEMPT_OFFT

        sti
        callq fire_deffered_actions
        cli

        /* More prioritized deffered actions occured ? */
        bt $CPU_SCHED_DEF_WORKS_F_IDX, %gs:CPU_SCHED_STAT_FLAGS_OFFT
        jc process_irq_deffered_actions

no_irq_deffered_works:
	  /* Check if preemption is possible right now. */
	  cmp $0, %gs:CPU_SCHED_STAT_PREEMPT_OFFT
	  jnz no_preemption_in_interrupt

	  bt $CPU_SCHED_NEED_RESCHED_F_IDX, %gs:CPU_SCHED_STAT_FLAGS_OFFT
	  jnc no_preemption_in_interrupt

	  /* OK, reschedule current task. */
	  callq schedule

no_preemption_in_interrupt:
	  /* Restore iteration flag. */
	  popq %rax

	  /* Now let's see if we need to check for userspace works. */
	  movq (%rsp), %rbx
	  cmpq $GDT_SEL(KCODE_DESCR), INT_FULL_OFFSET_TO_CS(%rbx)
	  je exit_from_irq

	  /* Userspace interrupts, need to check for userspace works. */
	  mov %gs:CPU_SCHED_STAT_USER_WORKS_OFFT, %rbx

	  cmp $0, (%rbx)
	  je exit_from_irq

	  /* Check if have already performed userspace works for this task. */
	  orq %rax,%rax
	  jnz exit_from_irq

	  /* Process userspace-related works. */
	  sti
	  movq $__INT_UWORK, %rdi
	  xorq %rsi, %rsi
	  movq %rsp, %rdx
	  callq handle_uworks
      cli

	  /* Since interrupts were enabled during executing userspace-related
	   * works, we must make sure that there are no pending deffered IRQ works
	   * with higher priorities than ours.
	   */
	  pushq $1
	  jmp process_irq_deffered_actions

exit_from_irq:
        sti
        RESTORE_ALL
        cmpq $GDT_SEL(KCODE_DESCR), 8 + INT_STACK_FRAME_CS_OFFT(%rsp)
        je 1f

        RESTORE_USER_SEGMENT_REGISTERS
        cli
        movl %gs:(CPU_SCHED_STAT_USER_GS_OFFT), %eax
        swapgs
        movl %eax, %gs

1:
        popq %rax
        /* Make sure IF is set after returning from interrupt context. */
        orq $(1 << 9),HW_INTERRUPT_CTX_RFLAGS_OFFT(%rsp)
        iretq

.align 16
__faults_table:
        GEN_INTRS_TABLE 0, 32, fault_entry_point

.align 16
__irqs_table:
         GEN_INTRS_TABLE 32, 255, irq_entry_point

