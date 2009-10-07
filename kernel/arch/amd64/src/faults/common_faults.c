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
 * (c) Copyright 2009 Dan Kruchinin <dk@jarios.org>
 *
 * mstring/amd64/faults/common_faults.c: contains routines for dealing with
 *   common x86_64 CPU fauls.
 *
 */

#include <config.h>
#include <arch/fault.h>
#include <arch/context.h>
#include <arch/assert.h>
#include <arch/cpu.h>
#include <mstring/kprintf.h>
#include <mstring/types.h>

void FH_bound_range(struct fault_ctx *fctx)
{
  kprintf_fault("Bound range fault\n");
  for (;;);
}

void FH_invalid_opcode(struct fault_ctx *fctx)
{  
  if (fctx->gprs->rax == ASSERT_MAGIC) {
    struct gpregs *gprs = fctx->gprs;
    
    kprintf_fault((char *)gprs->r10, (char *)gprs->r11,
                  (char *)gprs->r12, (int)gprs->r13);
    goto out;
  }

  kprintf_fault("Invalid opcode exception!!!\n");
  
out:
  kprintf_fault("assertion done\n");
  //fault_dump_regs(regs, stack_frame->rip);
  //show_stack_trace(stack_frame->old_rsp);
  for(;;);
}

void FH_device_not_avail(struct fault_ctx *fctx)
{
  kprintf_fault("Device not avail fault\n");
  for (;;);
}

void FH_breakpoint(struct fault_ctx *fctx)
{
  kprintf_fault("Breakpoint fault handler\n");
  for (;;);
}

void FH_debug(struct fault_ctx *fctx)
{
  kprintf_fault("Debug fault\n");
  for (;;);
}

void FH_alignment_check(struct fault_ctx *fctx)
{
  kprintf_fault("Alignment check foult\n");
  for (;;);
}

void FH_machine_check(struct fault_ctx *fctx)
{
  kprintf_fault("Machine check fault!\n");
  for (;;);
}

void FH_security_exception(struct fault_ctx *fctx)
{
  kprintf_fault("Security exception!\n");
  for (;;);
}

void FH_reserved(struct fault_ctx *fctx)
{
  kprintf_fault("Reserved fault handler!\n");
  for (;;);
}

void FH_nmi(struct fault_ctx *fctx)
{
  kprintf_fault("NMI fault handler!!!\n");
  for (;;);
}

void FH_double_fault(struct fault_ctx *fctx)
{
  uintptr_t fault_addr;

  fault_addr = read_cr2();
  kprintf_fault("Double fault handler: %p\n", fault_addr);
  for (;;);
}

void FH_invalid_tss(struct fault_ctx *fctx)
{
  kprintf_fault("INVALID TSS\n");
  for (;;);
}

void FH_stack_exception(struct fault_ctx *fctx)
{
  kprintf_fault("STACK EXCEPTION\n");
  for (;;);
}

void FH_general_protection_fault(struct fault_ctx *fctx)
{
  kprintf_fault("GPFAULT\n");
  for (;;);
}

void FH_floating_point_exception(struct fault_ctx *fctx)
{
  kprintf_fault("Floating point exception\n");
  for (;;);
}
