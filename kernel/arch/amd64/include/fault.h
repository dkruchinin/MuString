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
 * (c) Copyright 2009 Dan Kruchinin <dk@harios.org>
 *
 * include/mstring/amd64/fault.h: contains types and prototypes for dealing
 *                            with x86_64 CPU fauls.
 *
 */

#ifndef __MSTRING_ARCH_FAULT_H__
#define __MSTRING_ARCH_FAULT_H__ 

#include <arch/context.h>
#include <arch/seg.h>
#include <mstring/types.h>

enum fault_idx {
  FLT_DE  = 0, /* Divide-by-Zero-Error */
  FLT_DB  = 1, /* Debug */
  FLT_NMI = 2, /* Non-Maskable-Interrupt */
  FLT_BP  = 3, /* Breakpoint */
  FLT_OF  = 4, /* Overflow */
  FLT_BR  = 5, /* Bound-range */
  FLT_UD  = 6, /* Invalid-Opcode */
  FLT_NM  = 7, /* Device-Not-Available */
  FLT_DF  = 8, /* Double fault */
  /* #9 is reserved */
  FLT_TS = 10, /* Invalid TSS */
  FLT_NP = 11, /* Segment-Not-Present */
  FLT_SS = 12, /* SS register loads and stack references */
  FLT_GP = 13, /* General-Protection */
  FLT_PF = 14, /* Page-Fault */
  /* #15 is reserved. */
  FLT_MF = 16, /* x87 Floating-Point Exception */
  FLT_AC = 17, /* Alignment-Check */
  FLT_MC = 18, /* Machine-Check */
  FLT_XF = 19, /* SIMD Floating-Point */
  /* #20 - 29 are reserved. */
  FLT_SX = 30, /* Security Exception */
  /* #31 is reserved */
};

#define MIN_FAULT_IDX FLT_DE
#define MAX_FAULT_IDX 31
#define IDT_NUM_FAULTS (MAX_FAULT_IDX + 1)

struct fault_ctx {
  struct gpregs *gprs;
  struct intr_stack_frame *istack_frame;
  uint32_t errcode;
  enum fault_idx fault_num;
  void *old_rsp;
};

typedef void (*fault_handler_fn)(struct fault_ctx *fctx);

#define FAULT_IS_RESERVED(fidx)                         \
  (((fidx) == 9) || ((fidx) == 15) ||                   \
   (((fidx) > 19) && ((fidx) < 30)) || ((fidx) == 31))

#define IS_KERNEL_FAULT(fctx)\
  ((fctx)->istack_frame->cs == GDT_SEL(KCODE_DESCR))

INITCODE void arch_faults_init(void);
extern void __do_handle_fault(void *rsp, enum fault_idx fault_num);

extern void FH_devide_by_zero(struct fault_ctx *fctx);
extern void FH_debug(struct fault_ctx *fctx);
extern void FH_nmi(struct fault_ctx *fctx);
extern void FH_breakpoint(struct fault_ctx *fctx);
extern void FH_overflow(struct fault_ctx *fctx);
extern void FH_bound_range(struct fault_ctx *fctx);
extern void FH_invalid_opcode(struct fault_ctx *fctx);
extern void FH_device_not_avail(struct fault_ctx *fctx);
extern void FH_double_fault(struct fault_ctx *fctx);
extern void FH_invalid_tss(struct fault_ctx *fctx);
extern void FH_segment_not_present(struct fault_ctx *fctx);
extern void FH_stack_exception(struct fault_ctx *fctx);
extern void FH_general_protection_fault(struct fault_ctx *fctx);
extern void FH_page_fault(struct fault_ctx *fctx);
extern void FH_floating_point_exception(struct fault_ctx *fctx);
extern void FH_alignment_check(struct fault_ctx *fctx);
extern void FH_machine_check(struct fault_ctx *fctx);
extern void FH_simd_floating_point(struct fault_ctx *fctx);
extern void FH_security_exception(struct fault_ctx *fctx);
extern void FH_reserved(struct fault_ctx *fctx);

#endif /* !__MSTRING_ARCH_FAULT_H__  */

