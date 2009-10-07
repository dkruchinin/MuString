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
 * (c) Copyright 2009 Dan Kruchinin <dk@jarios.h>
 *
 * mstring/amd64/fault.c: contains routines for dealing with x86_64 CPU fauls
 *     and provides routines for dealing with exception fixups.
 *
 */

#include <arch/fault.h>
#include <arch/seg.h>
#include <arch/context.h>
#include <mstring/panic.h>
#include <mstring/bitwise.h>
#include <mstring/stddef.h>
#include <mstring/types.h>

extern uint8_t __faults_table[];
static fault_handler_fn fault_handlers[IDT_NUM_FAULTS];
static INITDATA int cur_ist = 0;

/*
 * On AMD64 there are two kinds of faults: some faults
 * has error code field on therir stack frame and some not.
 * To determine which faults push error code on stack frame
 * we introduce fault_with_errcode bitmask where each set bit
 * number corresponds to fault number having error code field.
 * On AMD64 there are 7 faults of such kind.
 */
static uint32_t faults_with_errcode =
  (POW2(FLT_DF) | POW2(FLT_TS) | POW2(FLT_NP) |
   POW2(FLT_SS) | POW2(FLT_GP) | POW2(FLT_PF) |
   POW2(FLT_AC));

#define GET_FAULTS_TABLE_ENTRY(idx)             \
  ((uintptr_t)&__faults_table + (idx) * 0x10)

#define SET_FAULT_HANDLER(idx, fn)              \
  fault_handlers[(idx)] = (fn)

#define FAULT_HAS_ERRCODE(fidx)                 \
  bit_test(&faults_with_errcode, fidx)

static INITCODE int get_ist_by_fault(enum fault_idx fidx)
{
  switch (fidx) {
      case FLT_DF:
        return ++cur_ist;
      default:
        break;
  }

  return 0;
}

static INITCODE void init_reserved_faults(void)
{
  int i;

  for (i = 0; i < IDT_NUM_FAULTS; i++) {
    if (FAULT_IS_RESERVED(i)) {
      SET_FAULT_HANDLER(i, FH_reserved);
    }
  }
}

static void fill_fault_context(struct fault_ctx *fctx, void *rsp,
                               enum fault_idx fault_num)
{
  uint8_t *p = rsp;

  /* Save fault index */
  fctx->fault_num = fault_num;

  /* Save pointer to purpose registers [%rax .. %r15] */
  fctx->gprs = (struct gpregs *)p;
  p += sizeof(struct gpregs);

  /*
   * If fault saves error code on the stack frame,
   * data saved by interrupt itself(i.e. interrupt stack frame)
   * will lie on 8 bytes higher.
   */
  if (FAULT_HAS_ERRCODE(fault_num)) {
    fctx->errcode = *(uint32_t *)p;
    p += 8;
  }
  else {
    fctx->errcode = 0;
  }

  /* Save pointer to interrupt stack frame */
  fctx->istack_frame = (struct intr_stack_frame *)p;
  p += sizeof(struct intr_stack_frame);

  /* An address RSP pointed to berfore fault occured. */
  fctx->old_rsp = p;
}

INITCODE void arch_faults_init(void)
{
  int slot, ist;

  for (slot = 0; slot < IDT_NUM_FAULTS; slot++) {
    ist = get_ist_by_fault(slot);
    idt_install_gate(slot, SEG_TYPE_INTR, SEG_DPL_KERNEL,
                     GET_FAULTS_TABLE_ENTRY(slot), ist);
  }

  SET_FAULT_HANDLER(FLT_DE,  FH_devide_by_zero);
  SET_FAULT_HANDLER(FLT_DB,  FH_debug);
  SET_FAULT_HANDLER(FLT_NMI, FH_nmi);
  SET_FAULT_HANDLER(FLT_BP,  FH_breakpoint);
  SET_FAULT_HANDLER(FLT_OF,  FH_overflow);
  SET_FAULT_HANDLER(FLT_BR,  FH_bound_range);
  SET_FAULT_HANDLER(FLT_UD,  FH_invalid_opcode);
  SET_FAULT_HANDLER(FLT_NM,  FH_device_not_avail);
  SET_FAULT_HANDLER(FLT_DF,  FH_double_fault);
  SET_FAULT_HANDLER(FLT_TS,  FH_invalid_tss);
  SET_FAULT_HANDLER(FLT_NP,  FH_segment_not_present);
  SET_FAULT_HANDLER(FLT_SS,  FH_stack_exception);
  SET_FAULT_HANDLER(FLT_GP,  FH_general_protection_fault);
  SET_FAULT_HANDLER(FLT_PF,  FH_page_fault);
  SET_FAULT_HANDLER(FLT_MF,  FH_floating_point_exception);
  SET_FAULT_HANDLER(FLT_AC,  FH_alignment_check);
  SET_FAULT_HANDLER(FLT_MC,  FH_machine_check);
  SET_FAULT_HANDLER(FLT_XF,  FH_simd_floating_point);
  SET_FAULT_HANDLER(FLT_SX,  FH_security_exception);

  init_reserved_faults();
}


void __do_handle_fault(void *rsp, enum fault_idx fault_num)
{
    struct fault_ctx fctx;

    if (unlikely((fault_num < MIN_FAULT_IDX) ||
                 (fault_num > MAX_FAULT_IDX))) {
        panic("Unexpected fault number: %d. Expected fault indices in "
              "range [%d, %d]", fault_num, MIN_FAULT_IDX, MAX_FAULT_IDX);
    }

    fill_fault_context(&fctx, rsp, fault_num);
    fault_handlers[fault_num](&fctx);    
}
