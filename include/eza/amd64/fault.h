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
 * (c) Copyright 2008 Michael Tsymbalyuk <mtzaurus@gmail.com>
 *
 * include/eza/amd64/fault.h: contains types and prototypes for dealing
 *                            with x86_64 CPU fauls.
 *
 */

#ifndef __ARCH_FAULT_H__
#define __ARCH_FAULT_H__ 

#include <mlibc/types.h>
#include <eza/arch/page.h>
#include <eza/arch/interrupt.h>
#include <eza/arch/context.h>
#include <eza/task.h>

enum __fault_ids {
  DE_FAULT = 0, /* Divide-by-Zero-Error */
  DB_FAULT = 1, /* Debug */
  NMI_FAULT = 2, /* Non-Maskable-Interrupt */
  BP_FAULT = 3, /* Breakpoint */
  OF_FAULT = 4, /* Overflow */
  BR_FAULT = 5, /* Bound-range */
  UD_FAULT = 6, /* Invalid-Opcode */
  NM_FAULT = 7, /* Device-Not-Available */
  DF_FAULT = 8, /* Double fault */
                /* #10 is reserved */
  TS_FAULT = 10, /* Invalid TSS */
  NP_FAULT = 11, /* Segment-Not-Present */
  SS_FAULT = 12, /* SS register loads and stack references */
  GP_FAULT = 13, /* General-Protection */
  PF_FAULT = 14, /* Page-Fault */
                 /* #15 is reserved. */
  MF_FAULT = 16, /* x87 Floating-Point Exception */
  AC_FAULT = 17, /* Alignment-Check */
  MC_FAULT = 18, /* Machine-Check */
  XF_FAULT = 19, /* SIMD Floating-Point */
                 /* #20 - 29 are reserved. */
  SX_FAULT = 30, /* Security Exception */
                 /* #31 is reserved */
};

/* Standard CPU fault handlers ranged [0 .. 31] */
extern void divide_by_zero_fault_handler(void);
extern void debug_fault_handler(void);
extern void nmi_fault_handler(void);
extern void breakpoint_fault_handler(void);
extern void overflow_fault_handler(void);
extern void bound_range_fault_handler(void);
extern void invalid_opcode_fault_handler(void);
extern void device_not_available_fault_handler(void);
extern void doublefault_fault_handler(void);
extern void coprocessor_segment_overrun_fault_handler(void);
extern void invalid_tss_fault_handler(void);
extern void segment_not_present_fault_handler(void);
extern void stack_fault_handler(void);
extern void general_protection_fault_handler(void);
extern void page_fault_fault_handler(void);
extern void reserved_exception_fault_handler(void);
extern void fpu_fault_handler(void);
extern void alignment_check_fault_handler(void);
extern void machine_check_fault_handler(void);
extern void simd_fault_handler(void);
extern void security_exception_fault_handler(void);

void install_fault_handlers(void);
void fault_dump_regs(regs_t *r, ulong_t rip);
void show_stack_trace(uintptr_t stack);

static inline struct __int_stackframe *arch_get_userspace_stackframe(task_t *task)
{
  return (struct __int_stackframe*)(task->kernel_stack.high_address-sizeof(struct __int_stackframe));
}

static inline long get_userspace_stack(task_t *task)
{
  return arch_get_userspace_stackframe(task)->old_rsp;
}

static inline long get_userspace_ip(task_t *task)
{
  return arch_get_userspace_stackframe(task)->rip;
}

#define kernel_fault(stack_frame) \
    (stack_frame->cs == gdtselector(KTEXT_DES))

uint64_t fixup_fault_address(uint64_t fault_address);
#endif

