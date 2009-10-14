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
 * (c) Copyright 2005,2008 Tirra <tirra.newly@gmail.com>
 * (c) Copyright 2008 Michael Tsymbalyuk <mtzaurus@gmail.com>
 * (c) Copyright 2008 Dmitry Gromada <gromada@jarios.org>
 *
 * /mstring/amd64/asm.S: low level assembler functions specific for amd64
 *                   architecture
 *
 */

#include <arch/context.h>
#include <arch/page.h>
#include <arch/seg.h>
#include <arch/current.h>
        
.code64

.text
.global set_efer_flag

.global arch_delay_loop
.global arch_fake_loop

.global return_from_common_interrupt
.global return_from_syscall
.global divide_by_zero_fault_handler
.global debug_fault_handler
.global nmi_fault_handler
.global breakpoint_fault_handler
.global overflow_fault_handler
.global bound_range_fault_handler
.global invalid_opcode_fault_handler
.global device_not_available_fault_handler
.global doublefault_fault_handler
.global invalid_tss_fault_handler
.global segment_not_present_fault_handler
.global stack_fault_handler
.global general_protection_fault_handler
.global page_fault_fault_handler
.global fpu_fault_handler
.global alignment_check_fault_handler
.global machine_check_fault_handler
.global simd_fault_handler
.global security_exception_fault_handler
.global hi_level_fault_handlers 

.global kthread_fork_path
.global user_fork_path
.global user_fork_path_test

arch_delay_loop:
0:	dec %rdi
	jnz 0b
	ret

arch_fake_loop:
0:	dec %rdi
	jz 0b
	ret
	
set_efer_flag:
	movq $0xc0000080, %rcx
	rdmsr
	btsl %edi, %eax
	wrmsr
	ret

kthread_fork_path:
    sti
    RESTORE_ALL
    pop %rax
    iretq

user_fork_path_test:
    xorq %rax,%rax
user_fork_path:
    xorq %rax,%rax
resume_userspace:
    RESTORE_ALL
    jmp userspace_ret_from_int

/* Declare table containing matches of interrupt vectors with irqs */
.comm vector_irq_table, 256

/* Initialize low-level IRQ stubs. Thank Linux team for a very good approach !
 */
.global irq_entrypoints_array 
.global low_irq_stubs 

.data
irq_entrypoints_array:
.text

v=0
low_irq_stubs:

.rept 256
1:  pushq %rax
    xor %rax,%rax
		movb (vector_irq_table + v),%al
    jmp common_interrupt

.data
    .quad 1b
.text
v=v+1
.endr

common_interrupt:
  ENTER_INTERRUPT_CTX(kernel_space_interrupt,INT_STACK_EXTRA_PUSHES)

  movq %rax,%rdi
  call do_irq

return_from_common_interrupt:
  /* To avoid nested schedulings (when current process is scheduled
   * during its schedule path, we disable interrupts when checking
   * for rescheduling to occur. Interrupts will be re-enabled either
   * in 'schedule()', or in 'arch_activate_task()' after context
   * switch completes.
   */
  cli

  /* Zero on top of the stack tells us that we haven't performed
   * userspace-related checks for current task.
   */
  pushq $0

  /* Tell the kernel that we've left interrupt context. */
  decq %gs:CPU_SCHED_STAT_IRQCNT_OFFT
  jnz no_preemption_in_interrupt

  /* OK, left the last nested IRQ, so check for deffered actions. */
process_irq_deffered_actions:
  /* Now check if there are some pending deffered actions. */
  bt $CPU_SCHED_DEF_WORKS_F_IDX, %gs:CPU_SCHED_STAT_FLAGS_OFFT
  jnc no_irq_deffered_works

  btr $CPU_SCHED_DEF_WORKS_F_IDX, %gs:CPU_SCHED_STAT_FLAGS_OFFT

  /* We shouldn't be preempted while processing deffered actions. */
  addq $1, %gs:CPU_SCHED_STAT_PREEMPT_OFFT

  sti
  call fire_deffered_actions
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
  call schedule

no_preemption_in_interrupt:
  /* Restore iteration flag. */
  popq %rax

  /* Now let's see if we need to check for userspace works. */
  mov (%rsp), %rbx
  add $INT_FULL_OFFSET_TO_CS,%rbx
  cmp $GDT_SEL(KCODE_DESCR),(%rbx)
  je dont_check_int_works

  /* Userspace interrupts, need to check for userspace works. */
  mov %gs:CPU_SCHED_STAT_USER_WORKS_OFFT, %rbx

  cmp $0, (%rbx)
  je dont_check_int_works

  /* Check if have already performed userspace works for this task. */
  orq %rax,%rax
  jnz dont_check_int_works

  /* Process userspace-related works. */
  sti
  movq $__INT_UWORK, %rdi
  xorq %rsi, %rsi
  movq %rsp, %rdx
  call handle_uworks

  cli
  /* Since interrupts were enabled during executing userspace-related
   * works, we must make sure that there are no pending deffered IRQ works
   * with higher priorities than ours.
   */
   pushq $1
   jmp process_irq_deffered_actions

dont_check_int_works:
  RESTORE_ALL
  /* Now we can enable interrupts. */
  cmp $GDT_SEL(KCODE_DESCR),INT_STACK_EXTRA_PUSHES+INT_STACK_FRAME_CS_OFFT(%rsp)
  je ret_from_int

  /* If we have come from user space, restore %gs and %ds.
   * Interrupts will be enabled after reloading %rflags during
   * execution of the IRETQ instruction.
   * We disable interrupts to avoid the use of user data segment
   * in case if another interrupt has occured.
   */
userspace_ret_from_int:
  /* Restore user segment registers. */
  RESTORE_USER_SEGMENT_REGISTERS

  cli
  /* Now restore user %gs */  
  mov %gs:(CPU_SCHED_STAT_USER_GS_OFFT),%eax
  swapgs
//1:      jmp 1b
  mov %eax,%gs
ret_from_int:
  popq %rax

  /* Make sure IF is set after returning from interrupt context. */
  orq $(1 << 9),HW_INTERRUPT_CTX_RFLAGS_OFFT(%rsp)
  iretq 

return_from_syscall:
  /* Before returning from syscall we should check for pending works.
   * Note that %rax contains return code.
   */
  mov %gs:CPU_SCHED_STAT_USER_WORKS_OFFT, %rbx

  cmp $0,(%rbx)
  je no_works_after_syscall

  pushq %rax
  movq $__SYCALL_UWORK, %rdi
  movq %rax, %rsi
  movq %rsp, %rdx
  addq $8, %rdx  /* Skip just saved %rax */
  call handle_uworks
  popq %rax

no_works_after_syscall:
  RESTORE_ALL

  /* Now restore user %gs */
  cli
  addq $8,%rsp /* Simulate our extra 'push %rax' */
  RESTORE_USER_SEGMENT_REGISTERS
  pushq %rax
  mov %gs:(CPU_SCHED_STAT_USER_GS_OFFT),%eax
  swapgs
  mov %eax,%gs

  jmp ret_from_int

/* Here goes handlers for CPU-specific fault handlers ranged at [0 .. 31]
 */
divide_by_zero_fault_handler:
  cli
  pushq %rax
  movl $0x0,%eax
common_exception_path:
  /* Since %rax is on the stack, we must add 8 bytes. */
  cmp $GDT_SEL(KCODE_DESCR),8+INT_STACK_FRAME_CS_OFFT(%rsp)
  je common_exception_path_kern
  swapgs
common_exception_path_kern:
  pushq %rax
x  movq online_cpus, %rax
  orq %rax, %rax
  popq %rax
  jz 1f
  sti
1:      
  SAVE_ALL
  /* No we have to prepare interrupt/exception stack frame.
   * Main job willbe done in 'SAVE_ALL', bu we must add 8 bytes to
   * skip %rax on the stack since we saved it before calling SAVE_ALL
   * %r12 contains the value of %rsp before aligning for saving XMM data.
   */
  mov %r12, %rdi;
  add $SAVED_GPR_SIZE + 8, %rdi

  /* Now we can call the handler. */
  call *hi_level_fault_handlers(,%rax,8)

  /* Now let's see if we need to check for userspace works. */
  mov (%rsp), %rbx
  add $INT_FULL_OFFSET_TO_CS,%rbx
  cmp $GDT_SEL(KCODE_DESCR),(%rbx)
  je dont_check_common_exception_uworks

  /* Process deferred userspace-related works */
  movq $__XCPT_ERR_UWORK, %rdi
  xorq %rsi, %rsi
  movq %rsp, %rdx
  call handle_uworks

dont_check_common_exception_uworks:
  RESTORE_ALL
  cli
  cmp $GDT_SEL(KCODE_DESCR),8+INT_STACK_FRAME_CS_OFFT(%rsp)
  je common_exception_path_kern_ret
  RESTORE_USER_SEGMENT_REGISTERS
  swapgs
common_exception_path_kern_ret:
  popq %rax
  iretq

error_code_exception_path:
  /* Since %rax is saved on the stack, we must add 8 bytes in
   * addition to 8 bytes that contain error code.
   */
  cmp $GDT_SEL(KCODE_DESCR),16+INT_STACK_FRAME_CS_OFFT(%rsp)
  je error_code_exception_path_kern
  swapgs
error_code_exception_path_kern:
  sti  
  SAVE_ALL
  /* Now we have to prepare interrupt/exception stack frame.
   * Main job willbe done in 'SAVE_ALL', bu we must add 8 bytes to
   * skip %rax on the stack since we saved it before calling SAVE_ALL
   * %r12 contains the value of %rsp before aligning for saving XMM data.
   */
  mov %r12, %rdi;
  add $SAVED_GPR_SIZE + 8, %rdi

  /* Now we can call the handler. */  
  call *hi_level_fault_handlers(,%rax,8)

  /* Now let's see if we need to check for userspace works. */
  mov (%rsp), %rbx
  add $INT_FULL_OFFSET_TO_CS_ERR,%rbx
  cmp $GDT_SEL(KCODE_DESCR),(%rbx)
  je dont_check_err_exception_uworks

  /* Userspace interrupts, need to check for userspace works. */
  mov %gs:CPU_SCHED_STAT_USER_WORKS_OFFT, %rbx
  cmp $0, (%rbx)
  je dont_check_err_exception_uworks

  /* Process deffered works. */
  movq $__XCPT_ERR_UWORK, %rdi
  xorq %rsi, %rsi
  movq %rsp, %rdx
  call handle_uworks

dont_check_err_exception_uworks:
  RESTORE_ALL

  cli
  cmp $GDT_SEL(KCODE_DESCR),16+INT_STACK_FRAME_CS_OFFT(%rsp)
  je error_code_exception_path_kern_ret
  RESTORE_USER_SEGMENT_REGISTERS
  swapgs
error_code_exception_path_kern_ret:
  popq %rax
  /* We have the error code on the top of stack, so remove it before
   * returning.
   */
  addq $0x8,%rsp
  iretq

debug_fault_handler:
  push %rax
  movl $0x1,%eax
  jmp common_exception_path

nmi_fault_handler:
  push %rax
  movl $0x2,%eax
  jmp common_exception_path

breakpoint_fault_handler:
  push %rax
  movl $0x3,%eax
  jmp common_exception_path

overflow_fault_handler:
  push %rax
  movl $0x4,%eax
  jmp common_exception_path

bound_range_fault_handler:
  push %rax
  movl $0x5,%eax
  jmp common_exception_path

invalid_opcode_fault_handler:
  push %rax
  movl $0x6,%eax
  jmp common_exception_path

device_not_available_fault_handler:
  push %rax
  movl $0x7,%eax
  jmp common_exception_path

doublefault_fault_handler:
  /* Double-fault is a one-way journey, so don't enable interrupts
   * and invoke handler directly.
   */
  movq %rsp,%rdi
  call doublefault_fault_handler_impl

invalid_tss_fault_handler:
  push %rax
  movl $10,%eax
  jmp common_exception_path        

segment_not_present_fault_handler:
  push %rax
  movl $11,%eax
  jmp common_exception_path

stack_fault_handler:
  push %rax
  movl $12,%eax
  jmp common_exception_path

general_protection_fault_handler:
  push %rax
  movl $13,%eax
  jmp error_code_exception_path

page_fault_fault_handler:
  push %rax
  movl $14,%eax
  jmp error_code_exception_path

fpu_fault_handler:
  push %rax
  movl $16,%eax
  jmp common_exception_path

alignment_check_fault_handler:
  push %rax
  movl $17,%eax
  jmp common_exception_path

machine_check_fault_handler:
  push %rax
  movl $18,%eax
  jmp common_exception_path

simd_fault_handler:
  push %rax
  movl $19,%eax
  jmp common_exception_path

security_exception_fault_handler:
  push %rax
  movl $30,%eax
  jmp common_exception_path 

        
/* Table that represents hi-level exception routines written in C.
 *            DON'T CHANGE THE ORDER IN THIS TABLE !
 */

hi_level_fault_handlers:
.quad divide_by_zero_fault_handler_impl        /* 0 */
.quad debug_fault_handler_impl                 /* 1 */
.quad nmi_fault_handler_impl                   /* 2 */
.quad breakpoint_fault_handler_impl            /* 3 */
.quad overflow_fault_handler_impl              /* 4 */
.quad bound_range_fault_handler_impl           /* 5 */
.quad invalid_opcode_fault_handler_impl        /* 6 */
.quad device_not_available_fault_handler_impl  /* 7 */

.quad doublefault_fault_handler_impl           /* 8 */
.quad reserved_fault_handler_impl              /* 9 */
.quad invalid_tss_fault_handler_impl           /* 10 */
.quad segment_not_present_fault_handler_impl   /* 11 */
.quad stack_fault_handler_impl                 /* 12 */
.quad general_protection_fault_handler_impl    /* 13 */
.quad page_fault_fault_handler_impl            /* 14 */
.quad reserved_fault_handler_impl              /* 15 */

.quad fpu_fault_handler_impl                   /* 16 */
.quad alignment_check_fault_handler_impl       /* 17 */
.quad machine_check_fault_handler_impl         /* 18 */
.quad simd_fault_handler_impl                  /* 19 */
.quad reserved_fault_handler_impl              /* 20 */
.quad reserved_fault_handler_impl              /* 21 */
.quad reserved_fault_handler_impl              /* 22 */
.quad reserved_fault_handler_impl              /* 23 */

.quad reserved_fault_handler_impl              /* 24 */
.quad reserved_fault_handler_impl              /* 25 */
.quad reserved_fault_handler_impl              /* 26 */
.quad reserved_fault_handler_impl              /* 27 */
.quad reserved_fault_handler_impl              /* 28 */
.quad reserved_fault_handler_impl              /* 29 */
.quad security_exception_fault_handler_impl    /* 30 */
.quad reserved_fault_handler_impl              /* 31 */
