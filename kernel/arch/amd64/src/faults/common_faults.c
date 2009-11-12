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
  fault_describe("BOUND-RANGE", fctx);
  fault_dump_info(fctx);
  __stop_cpu();
}

void FH_invalid_opcode(struct fault_ctx *fctx)
{  
  if (fctx->gprs->rax == ASSERT_MAGIC) {
    struct gpregs *gprs = fctx->gprs;

    kprintf_fault("================= [KERNEL ASSERTION] =================\n  ");
    kprintf_fault((char *)gprs->r10, (char *)gprs->r11,
                  (char *)gprs->r12, (int)gprs->r13);
    fault_dump_info(fctx);
    __stop_cpu();
  }

  fault_describe("INVALID OPCODE", fctx);
  fault_dump_info(fctx);
  __stop_cpu();
}

void FH_device_not_avail(struct fault_ctx *fctx)
{
  fault_describe("DEVICE NOT AVAILABLE", fctx);
  fault_dump_info(fctx);
  __stop_cpu();
}

void FH_breakpoint(struct fault_ctx *fctx)
{
  fault_describe("BREAKPOINT EXCEPTION", fctx);
  fault_dump_info(fctx);
  __stop_cpu();
}

void FH_debug(struct fault_ctx *fctx)
{
  /* TODO DK: implement debug interface... */
  fault_describe("DEBUG FAULT", fctx);
  fault_dump_info(fctx);
  __stop_cpu();
}

void FH_alignment_check(struct fault_ctx *fctx)
{
  fault_describe("ALIGNMENT-CHECK", fctx);
  fault_dump_info(fctx);
  __stop_cpu();
}

void FH_machine_check(struct fault_ctx *fctx)
{
  fault_describe("MACHINE-CHECK", fctx);
  fault_dump_info(fctx);
  __stop_cpu();
}

void FH_security_exception(struct fault_ctx *fctx)
{
  fault_describe("SECURITY EXCEPTION", fctx);
  fault_dump_info(fctx);
  __stop_cpu();
}

void FH_reserved(struct fault_ctx *fctx)
{
  kprintf_fault("Reserved fault handler!\n");
  for (;;);
}

void FH_nmi(struct fault_ctx *fctx)
{
  fault_describe("NMI EXCEPTION", fctx);
  fault_dump_info(fctx);
  __stop_cpu();
}

void FH_double_fault(struct fault_ctx *fctx)
{
  fault_describe("DOUBLE FAULT", fctx);
  fault_dump_info(fctx);
  __stop_cpu();
}

void FH_invalid_tss(struct fault_ctx *fctx)
{
  fault_describe("INVALID TSS", fctx);
  fault_dump_info(fctx);
  __stop_cpu();
}

void FH_stack_exception(struct fault_ctx *fctx)
{
  fault_describe("STACK EXCEPTION", fctx);
  fault_dump_info(fctx);
  __stop_cpu();
}

void FH_general_protection_fault(struct fault_ctx *fctx)
{
  fault_describe("GPF", fctx);
  fault_dump_info(fctx);
  __stop_cpu();
}

void FH_floating_point_exception(struct fault_ctx *fctx)
{
  kprintf_fault("Floating point exception\n");
  for (;;);
}
