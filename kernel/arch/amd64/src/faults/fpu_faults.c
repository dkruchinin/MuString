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
 * (c) Copytight 2009 Dan Kruchinin <dk@jarios.org>
 *
 * mstring/amd64/faults/fpu_faults.c: contains routines for dealing with
 *  FPU-related x86_64 CPU fauls.
 *
 */

#include <arch/context.h>
#include <arch/fault.h>
#include <mstring/kprintf.h>
#include <mstring/types.h>

void FH_devide_by_zero(struct fault_ctx *fctx)
{
  /* TODO DK: send SIGFPE in case of user fault */
  fault_describe("DIVIDE-BY-ZERO FAULT", fctx);
  fault_dump_info(fctx);
  __stop_cpu();
}

void FH_overflow(struct fault_ctx *fctx)
{
  fault_describe("OVERFLOW EXCEPTION", fctx);
  fault_dump_info(fctx);
  __stop_cpu();
}

void FH_segment_not_present(struct fault_ctx *fctx)
{
  fault_describe("SEGMENT-NOT-PRESENT", fctx);
  fault_dump_info(fctx);
  __stop_cpu();
}

void FH_simd_floating_point(struct fault_ctx *fctx)
{
  /*
   * TODO DK: display more precise information about fault reason
   * by parsing exception mask in the FPU control word.
   */
  fault_describe("SIMD FLOATING POINT", fctx);
  fault_dump_info(fctx);
  __stop_cpu();
}

