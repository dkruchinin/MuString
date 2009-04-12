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
  uint8_t ist;
};

/* The list of exceptions we want to install. */
static struct __fault_descr_t faults_to_install[] = {
  {DE_FAULT, divide_by_zero_fault_handler,0},
  {DB_FAULT, debug_fault_handler,0},
  {NMI_FAULT, nmi_fault_handler,0},
  {BP_FAULT, breakpoint_fault_handler,0},
  {OF_FAULT, overflow_fault_handler,0},
  {BR_FAULT, bound_range_fault_handler,0},
  {UD_FAULT, invalid_opcode_fault_handler,0},
  {NM_FAULT, device_not_available_fault_handler,0},
  {DF_FAULT, doublefault_fault_handler,1},
  {TS_FAULT, invalid_tss_fault_handler,0},
  {NP_FAULT, segment_not_present_fault_handler,0},
  {SS_FAULT, stack_fault_handler,0},
  {GP_FAULT, general_protection_fault_handler,0},
  {PF_FAULT, page_fault_fault_handler,0},
  {MF_FAULT, fpu_fault_handler,0},
  {AC_FAULT, alignment_check_fault_handler,0},
  {MC_FAULT, machine_check_fault_handler,0},
  {XF_FAULT, simd_fault_handler,0},
  {SX_FAULT, security_exception_fault_handler,0},
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
  int r;
  struct __fault_descr_t *fd=faults_to_install;

  /* Install all known fault handlers. */
  while( fd->handler ) {
    r = install_interrupt_gate(fd->slot,(uintptr_t)fd->handler,
                               PROT_RING_0,fd->ist);
    if( r != 0 ) {
      panic( "Can't install fault handler #%d", fd->slot );
    }
    fd++;
  }
}

void fault_dump_regs(regs_t *r, ulong_t rip)
{
  if (likely(is_cpu_online(cpu_id()))) {
    kprintf_fault("[CPU #%d] Current task: PID=%d, TID=0x%X\n",
            cpu_id(),current_task()->pid,current_task()->tid);
    kprintf_fault( " Task short name: '%s'\n",current_task()->short_name);
  }
  
  kprintf_fault(" RAX: %p, RBX: %p, RDI: %p, RSI: %p\n RDX: %p, RCX: %p\n",
          r->rax,r->gpr_regs.rbx,
          r->gpr_regs.rdi,r->gpr_regs.rsi,
          r->gpr_regs.rdx,r->gpr_regs.rcx);
  kprintf_fault(" RIP: %p\n",rip);
}

void show_stack_trace(uintptr_t stack)
{
  int i;

  kprintf_fault("\nTop %d words of kernel stack (RSP=%p).\n\n",
                CONFIG_NUM_STACKWORDS, stack);
  
  for(i = 0; i < CONFIG_NUM_STACKWORDS; i++) {
    stack += sizeof(uintptr_t);
    kprintf_fault("  <%p>\n", *(long *)stack);    
  }
}

