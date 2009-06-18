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
 * (c) Copyright 2006,2007,2008 MString Core Team <http://mstring.jarios.org>
 * (c) Copyright 2008 Tirra <tirra.newly@gmail.com>
 * (c) Copyright 2008 Michael Tsymbalyuk <mtzaurus@gmail.com>
 *
 * include/mstring/amd64/context.h: structure definion for context and related stuff
 *                              assembler macros and some constants
 *
 */

#ifndef __ARCH_CONTEXT_H__
#define __ARCH_CONTEXT_H__ /* there are several context.h(es) */

#define __SYCALL_UWORK     0  /**< Syscall-related works **/
#define __INT_UWORK        1  /**< Interrupt-related works **/
#define __XCPT_NOERR_UWORK 2  /**< Exception-related works (no error code on the stack) **/
#define __XCPT_ERR_UWORK   3  /**< Exception-related works (error code on the stack)  **/

#define OFFSET_SP   0x0
#define OFFSET_PC   0x8
#define OFFSET_RBX  0x10
#define OFFSET_RBP  0x18
#define OFFSET_R12  0x20
#define OFFSET_R13  0x28
#define OFFSET_R14  0x30
#define OFFSET_R15  0x38
#define OFFSET_IPL  0x40

#define OFFSET_TLS  OFFSET_IPL

/* Save all general purpose registers  */
#define SAVE_GPR \
    pushq %r8; \
    pushq %r9; \
    pushq %r10; \
    pushq %r11; \
    pushq %r12; \
    pushq %r13; \
    pushq %r14; \
    pushq %r15; \
    pushq %rbx; \
    pushq %rcx; \
    pushq %rdx; \
    pushq %rdi; \
    pushq %rsi; \
    pushq %rbp; \

#define RESTORE_GPR \
    popq %rbp; \
    popq %rsi; \
    popq %rdi; \
    popq %rdx; \
    popq %rcx; \
    popq %rbx; \
    popq %r15; \
    popq %r14; \
    popq %r13; \
    popq %r12; \
    popq %r11; \
    popq %r10; \
    popq %r9; \
    popq %r8; \

#define NUM_GPR_SAVED 14
#define SAVED_GPR_SIZE (NUM_GPR_SAVED * 8)

/* We assume that all GRPs were saved earlier !
 * Allocate an area for saving MMX & FX state. Finally, we must adjust %rdi
 * to point just after saved GPRs area.
 */
#define SAVE_MM \
  mov %rsp, %r12;                               \
  sub $512,%rsp;                                \
  movq $0xfffffffffffffff0, %r14;               \
  andq %r14,%rsp;                               \
  fxsave (%rsp);                                \
  pushq %r12

#define RESTORE_MM \
  popq %r12; \
  fxrstor (%rsp); \
  movq %r12, %rsp;

/* NOTE: SAVE_MM initializes %rsi so that it points to iterrupt/exception stack frame. */
#define SAVE_ALL \
  SAVE_GPR \
  SAVE_MM \


#define RESTORE_ALL \
  RESTORE_MM \
  RESTORE_GPR

#define SAVED_REGISTERS_SIZE \
   ((NUM_GPR_SAVED)*8)

/* Offsets to parts of CPU exception stack frames. */
#define INT_STACK_FRAME_CS_OFFT 8

/* assembler macros for save and restore context */
#ifdef __ASM__

.macro CONTEXT_SAVE_ARCH_CORE  xc:req pc:req
  movq \pc, OFFSET_PC(\xc)
  movq %rsp, OFFSET_SP(\xc)
  movq %rbx, OFFSET_RBX(\xc)
  movq %rbp, OFFSET_RBP(\xc)
  movq %r12, OFFSET_R12(\xc)
  movq %r13, OFFSET_R13(\xc)
  movq %r14, OFFSET_R14(\xc)
  movq %r15, OFFSET_R15(\xc)
.endm

.macro CONTEXT_RESTORE_ARCH_CORE  xc:req pc:req
  movq OFFSET_R15(\xc), %r15
  movq OFFSET_R14(\xc), %r14
  movq OFFSET_R13(\xc), %r13
  movq OFFSET_R12(\xc), %r12
  movq OFFSET_RBP(\xc), %rbp
  movq OFFSET_RBX(\xc), %rbx
  movq OFFSET_SP(\xc), %rsp
  movq OFFSET_PC(\xc), \pc
.endm

#endif /* __ASM__ */

/* amd64 specific note: ABI describes that stack
 * must be aligned to 16 byte , this can affect va_arg and so on ...
 */
#define SP_DELTA  16

/* Kernel-space task context related stuff. */
#define ARCH_CTX_CR3_OFFSET     0x0
#define ARCH_CTX_RSP_OFFSET     0x8
#define ARCH_CTX_FS_OFFSET      0x10
#define ARCH_CTX_GS_OFFSET      0x18
#define ARCH_CTX_ES_OFFSET      0x20
#define ARCH_CTX_DS_OFFSET      0x28
#define ARCH_CTX_URSP_OFFSET    0x30
#define ARCH_CTX_UWORKS_OFFSET  0x38
#define ARCH_CTX_PTD_OFFSET     0x40

#ifdef __ASM__

/* extra bytes on the stack after CPU exception stack frame: %rax */
#define INT_STACK_EXTRA_PUSHES  8

/* Offset to CS selecetor in case full context is saved on the stack */
#define INT_FULL_OFFSET_TO_CS (SAVED_GPR_SIZE+8+INT_STACK_FRAME_CS_OFFT)
#define INT_FULL_OFFSET_TO_CS_ERR (SAVED_GPR_SIZE+8+INT_STACK_FRAME_CS_OFFT+8)

#include <arch/current.h>
#include <arch/page.h>

/* Since we'll always turn turn on interrupts after executing IRETQ,
 * we should know where saved RFLAGS is located.
 */
#define HW_INTERRUPT_CTX_RIP_OFFT     0x0
#define HW_INTERRUPT_CTX_CS_OFFT      0x8
#define HW_INTERRUPT_CTX_RFLAGS_OFFT  0x10
#define HW_INTERRUPT_CTX_RSP_OFFT     0x18
#define HW_INTERRUPT_CTX_SS_OFFT      0x20

#define HW_INTERRUPT_CTX_SIZE         0x28

#define SAVE_AND_LOAD_SEGMENT_REGISTERS         \
   mov %ds, %gs:CPU_SCHED_STAT_USER_DS_OFFT;            \
   mov %es, %gs:CPU_SCHED_STAT_USER_ES_OFFT;            \
   mov %fs, %gs:CPU_SCHED_STAT_USER_FS_OFFT;            \
   mov %gs, %gs:CPU_SCHED_STAT_USER_GS_OFFT;            \
   mov %gs:(CPU_SCHED_STAT_KERN_DS_OFFT),%ds;   \

#define RESTORE_USER_SEGMENT_REGISTERS          \
   mov %gs:(CPU_SCHED_STAT_USER_DS_OFFT),%ds; \
   mov %gs:(CPU_SCHED_STAT_USER_ES_OFFT),%es; \
   mov %gs:(CPU_SCHED_STAT_USER_FS_OFFT),%fs; \
   pushq %rax;     \
   pushq %rcx;     \
   pushq %rdx;     \
   mov %gs:(CPU_SCHED_STAT_USER_PTD_OFFT),%rax; \
   movq $0xC0000100, %rcx; \
   movq %rax,%rdx; \
   shr $32,%rdx;\
   wrmsr; \
   popq %rdx; \
   popq %rcx; \
   popq %rax; \

#define ENTER_INTERRUPT_CTX(label,extra_pushes) \
	cmp $GDT_SEL(KCODE_DESCR),extra_pushes+INT_STACK_FRAME_CS_OFFT(%rsp) ;\
	je label;                                                           \
    swapgs ;                                                            \
    SAVE_AND_LOAD_SEGMENT_REGISTERS                                     \
    label:	;                                                           \
	incq %gs:CPU_SCHED_STAT_IRQCNT_OFFT ;                               \
	SAVE_ALL ;                                                          \
	sti

#define COMMON_INTERRUPT_EXIT_PATH \
         jmp return_from_common_interrupt;
     
#endif

#ifndef __ASM__

#include <mstring/types.h>
#include <arch/seg.h>

#define KERNEL_RFLAGS (DEFAULT_RFLAGS_VALUE | (3 << RFLAGS_IOPL_BIT))
     /* The most sensetive bits in RFLAGS.  */
#define RFLAGS_IOPL_BIT 12
#define RFLAGS_INT_BIT 9

/* Default RFLAGS: IOPL=0, all interrupts are enabled. */
#define DEFAULT_RFLAGS_VALUE (0 | (1 << RFLAGS_INT_BIT))

/* Initial RFLAGS value for new user tasks. */
#define USER_RFLAGS DEFAULT_RFLAGS_VALUE

/* Initial RFLAGS value for new kernel tasks: IOPL=3. */
#define KERNEL_RFLAGS (DEFAULT_RFLAGS_VALUE | (3 << RFLAGS_IOPL_BIT))

     
typedef struct __context_t { /* others don't interesting... */
  uintptr_t sp;
  uintptr_t pc;

  uint64_t rbx;
  uint64_t rbp;
  uint64_t r12;
  uint64_t r13;
  uint64_t r14;
  uint64_t r15;

  ipl_t ipl;
} __attribute__ ((packed)) context_t;

typedef struct __arch_context_t {
  uintptr_t cr3, rsp, fs, gs, es, ds, user_rsp;
  uintptr_t uworks;
  uintptr_t per_task_data;
  tss_t *tss;
  uintptr_t ldt;
  uint16_t tss_limit;
  uint16_t ldt_limit;
} arch_context_t;

#define arch_set_uworks_bit(ctx,bit)                    \
  __asm__ __volatile__(                                 \
    "bts %0,%1":: "r"((bit)),                           \
  "m" ((((arch_context_t *)(ctx))->uworks))  )

#define arch_clear_uworks_bit(ctx,bit)                    \
  __asm__ __volatile__(                                   \
    "btr %0,%1":: "r"((bit)),                             \
    "m"(((arch_context_t *)(ctx))->uworks)  )

#define arch_read_pending_uworks(ctx) ((arch_context_t *)(ctx))->uworks

/* Structure that represents GPRs on the stack upon entering
 * kernel mode during a system call.
 */
struct __gpr_regs {
  uint64_t rbp, rsi, rdi, rdx, rcx, rbx;
  uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
};

struct __int_stackframe {
  uint64_t rip, cs, rflags, old_rsp, old_ss;
};

typedef struct __regs {
  /* Kernel-saved registers. */
  struct __gpr_regs gpr_regs;
  uint64_t rax;

  /* CPU-saved registers. */
  struct __int_stackframe int_frame;
} regs_t;

#endif /* __ASM__ */

#endif /* __ARCH_CONTEXT_H__ */

