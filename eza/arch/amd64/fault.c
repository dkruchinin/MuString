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
 * eza/amd64/fault.c: contains routines for dealing with x86_64 CPU fauls
 *     and provides routines for dealing with exception fixups.
 *
 */

#include <eza/arch/types.h>
#include <eza/arch/page.h>
#include <eza/arch/fault.h>
#include <eza/arch/mm_types.h>
#include <eza/arch/interrupt.h>
#include <eza/kernel.h>
#include <mlibc/kprintf.h>
#include <eza/arch/mm.h>
#include <eza/smp.h>

/* Markers of exception table. */
ulong_t __ex_table_start,__ex_table_end;

struct __fixup_record_t {
  uint64_t fault_address,fixup_address;
}; 

struct __fault_descr_t {
  uint32_t slot;
  void (*handler)();
};

/* The list of exceptions we want to install. */
static struct __fault_descr_t faults_to_install[] = {
  {DE_FAULT, divide_by_zero_fault_handler},
  {DB_FAULT, debug_fault_handler},
  {NMI_FAULT, nmi_fault_handler},
  {BP_FAULT, breakpoint_fault_handler},
  {OF_FAULT, overflow_fault_handler},
  {BR_FAULT, bound_range_fault_handler},
  {UD_FAULT, invalid_opcode_fault_handler},
  {NM_FAULT, device_not_available_fault_handler},
  {DF_FAULT, doublefault_fault_handler},
  {TS_FAULT, invalid_tss_fault_handler},
  {NP_FAULT, segment_not_present_fault_handler},
  {SS_FAULT, stack_fault_handler},
  {GP_FAULT, general_protection_fault_handler},
  {PF_FAULT, page_fault_fault_handler},
  {MF_FAULT, fpu_fault_handler},
  {AC_FAULT, alignment_check_fault_handler},
  {MC_FAULT, machine_check_fault_handler},
  {XF_FAULT, simd_fault_handler},
  {SX_FAULT, security_exception_fault_handler},
  {0,0},
};

uint64_t fixup_fault_address(uint64_t fault_address)
{
  struct __fixup_record_t *fr=(struct __fixup_record_t *)&__ex_table_start;
  struct __fixup_record_t *end=(struct __fixup_record_t *)&__ex_table_end;

  while( fr<end ) {
    if( fr->fault_address == fault_address ) {
      return fr->fixup_address;
    }
    fr++;
  }
  return 0;
}

void install_fault_handlers(void)
{
  int idx, r;

  /* Install all known fault handlers. */
  for( idx = 0; faults_to_install[idx].handler != NULL; idx++ ) {
    r = install_trap_gate(faults_to_install[idx].slot,
                          (uintptr_t)faults_to_install[idx].handler,PROT_RING_0,0);
    if(r != 0) {
      panic( "Can't install fault handler #%d", faults_to_install[idx].slot );
    }
  }
}

