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
 * (c) Copyright 2008 Michael Tsymbalyuk <mtzaurus@gmail.com>
 *
 * eza/amd64/fault.c: contains routines for dealing with x86_64 CPU fauls.
 *
 */

#include <eza/arch/types.h>
#include <eza/arch/page.h>
#include <eza/arch/fault.h>
#include <eza/arch/mm_types.h>
#include <eza/arch/interrupt.h>
#include <eza/kernel.h>
#include <mlibc/kprintf.h>

#define kernel_fault(f) \
    (f->cs == gdtselector(KTEXT_DES))

#define get_fault_address(x) \
    __asm__ __volatile__( "movq %%cr2, %0" : "=r"(x) )

typedef struct __fault_descr {
  uint32_t slot;
  void (*handler)();
} fault_descr_t;

/* The list of exceptions we want to install. */
static fault_descr_t faults_to_install[] = {
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


void divide_by_zero_fault_handler_impl(interrupt_stack_frame_t *stack_frame)
{
  kprintf( "  [!!] #DE exception raised !\n" );
}

void debug_fault_handler_impl(interrupt_stack_frame_t *stack_frame)
{
  kprintf( "  [!!] #Debug exception raised !\n" );
}

void nmi_fault_handler_impl(interrupt_stack_frame_t *stack_frame)
{
  kprintf( "  [!!] #NMI exception raised !\n" );
}

void breakpoint_fault_handler_impl(interrupt_stack_frame_t *stack_frame)
{
  kprintf( "  [!!] #Breakpoint exception raised !\n" );
}

void overflow_fault_handler_impl(void)
{
  kprintf( "  [!!] #Overflow exception raised !\n" );
}

void bound_range_fault_handler_impl(interrupt_stack_frame_t *stack_frame)
{
    kprintf( "  [!!] #Bound range exception raised !\n" );
}

void invalid_opcode_fault_handler_impl(interrupt_stack_frame_t *stack_frame)
{
    kprintf( "  [!!] #Invalid opcode exception raised ! (%p)\n", stack_frame->rip );
}

void device_not_available_fault_handler_impl(interrupt_stack_frame_t *stack_frame)
{
    kprintf( "  [!!] #Dev not available exception raised !\n" );
}

void doublefault_fault_handler_impl(interrupt_stack_frame_err_t *stack_frame)
{
    kprintf( "  [!!] #Double fault exception raised !\n" );
}

void coprocessor_segment_overrun_fault_handler_impl(interrupt_stack_frame_t *stack_frame)
{
    kprintf( "  [!!] #FPU segment overrun exception raised !\n" );
}

void invalid_tss_fault_handler_impl(interrupt_stack_frame_err_t *stack_frame)
{
    kprintf( "  [!!] #Invalid TSS exception raised !\n" );
}

void segment_not_present_fault_handler_impl(interrupt_stack_frame_err_t *stack_frame)
{
    kprintf( "  [!!] #Segment not present exception raised !\n" );
}

void stack_fault_handler_impl(interrupt_stack_frame_err_t *stack_frame)
{
    kprintf( "  [!!] #Stack exception raised !\n" );
}

void general_protection_fault_handler_impl(interrupt_stack_frame_err_t *stack_frame)
{
  if( kernel_fault(stack_frame) ) {
    kprintf( "#GPF in kernel mode: RIP = 0x%X\n", stack_frame->rip );    
  }

  kprintf( "[!!!] Unhandled GPF exception ! Stopping (CODE: %d) ...\n",
           stack_frame->error_code );
  l1: goto l1;
}

void page_fault_fault_handler_impl(interrupt_stack_frame_err_t *stack_frame)
{
    uint64_t a;
  if( kernel_fault(stack_frame) ) {
    kprintf( "#PF in kernel mode: RIP = 0x%X\n", stack_frame->rip );    
  }
  get_fault_address(a);
  kprintf( "[!!!] Unhandled PF exception ! Stopping (CODE: %d, See page 225). Address: %p\n",
           stack_frame->error_code, a);
  l1: goto l1;

}

void reserved_exception_fault_handler_impl(interrupt_stack_frame_t *stack_frame)
{
    kprintf( "  [!!] #Reserved exception raised !\n" );
}

void fpu_fault_handler_impl(interrupt_stack_frame_t *stack_frame)
{
    kprintf( "  [!!] #FPU exception raised !\n" );
}

void alignment_check_fault_handler_impl(interrupt_stack_frame_err_t *stack_frame)
{
    kprintf( "  [!!] #Alignment check exception raised !\n" );
}

void machine_check_fault_handler_impl(interrupt_stack_frame_t *stack_frame)
{
    kprintf( "  [!!] #Machine check exception raised !\n" );
}

void simd_fault_handler_impl(interrupt_stack_frame_t *stack_frame)
{
    kprintf( "  [!!] #SIMD exception raised !\n" );
}

void security_exception_fault_handler_impl(interrupt_stack_frame_t *stack_frame)
{
    kprintf( "  [!!] #Security exception raised !\n" );
}

void reserved_fault_handler_impl(interrupt_stack_frame_t *stack_frame)
{
  kprintf( "  [!!] #Reserved fault exception raised !\n" );  
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

