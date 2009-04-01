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
 * eza/amd64/faults/common_faults.c: contains routines for dealing with
 *   common x86_64 CPU fauls.
 *
 */

#include <eza/arch/types.h>
#include <eza/arch/page.h>
#include <eza/arch/fault.h>
#include <eza/arch/interrupt.h>
#include <eza/kernel.h>
#include <mlibc/kprintf.h>
#include <eza/arch/mm.h>
#include <eza/arch/fault.h>
#include <eza/smp.h>
#include <eza/kconsole.h>
#include <eza/arch/cpu.h>
#include <eza/arch/assert.h>

void bound_range_fault_handler_impl(interrupt_stack_frame_t *stack_frame)
{
    kprintf_fault( "  [!!] #Bound range exception raised !\n" );
    for(;;);
}

void invalid_opcode_fault_handler_impl(interrupt_stack_frame_t *stack_frame)
{
  regs_t *regs = (regs_t *)(((uintptr_t)stack_frame)-sizeof(struct __gpr_regs)-8);
  
  if (regs->rax == ASSERT_MAGIC) {
    kprintf_fault((char *)regs->gpr_regs.r10, (char *)regs->gpr_regs.r11,
            (char *)regs->gpr_regs.r12, (int)regs->gpr_regs.r13);
    goto out;
  }
  
  kprintf_fault("Invalid opcode exception!!!\n");

  out:
  fault_dump_regs(regs, stack_frame->rip);
  show_stack_trace(stack_frame->old_rsp);
  for(;;);
}

void device_not_available_fault_handler_impl(interrupt_stack_frame_t *stack_frame)
{
    kprintf_fault( "  [!!] #Dev not available exception raised !\n" );
    for(;;);
}

void breakpoint_fault_handler_impl(interrupt_stack_frame_t *stack_frame)
{
  kprintf_fault( "  [!!] #Breakpoint exception raised !\n" );
  for(;;);
}

void debug_fault_handler_impl(interrupt_stack_frame_t *stack_frame)
{
  kprintf_fault( "  [!!] #Debug exception raised !\n" );
  for(;;);
}

void alignment_check_fault_handler_impl(interrupt_stack_frame_err_t *stack_frame)
{
    kprintf_fault( "  [!!] #Alignment check exception raised !\n" );
    for(;;);
}

void machine_check_fault_handler_impl(interrupt_stack_frame_t *stack_frame)
{
    kprintf_fault( "  [!!] #Machine check exception raised !\n" );
    for(;;);
}

void security_exception_fault_handler_impl(interrupt_stack_frame_t *stack_frame)
{
    kprintf_fault( "  [!!] #Security exception raised !\n" );
    for(;;);
}

void reserved_fault_handler_impl(interrupt_stack_frame_t *stack_frame)
{
  kprintf_fault( "  [!!] #Reserved fault exception raised !\n" );  
  for(;;);
}

void nmi_fault_handler_impl(interrupt_stack_frame_t *stack_frame)
{
  kprintf_fault( "  [!!] #NMI exception raised !\n" );
  for(;;);
}

void doublefault_fault_handler_impl(interrupt_stack_frame_err_t *stack_frame)
{ 
  //char b[512];
  uintptr_t inval_addr;

  __asm__ __volatile__("movq %%cr2, %0" : "=r"(inval_addr));  
  kprintf_fault("[!!] Fatal double fault exception! RIP=%p. (Inval addr = %p) CPU stopped.\n",
                stack_frame->rip, inval_addr);  
  for (;;);
}

void reserved_exception_fault_handler_impl(interrupt_stack_frame_t *stack_frame)
{
    kprintf_fault( "  [!!] #Reserved exception raised !\n" );
    for(;;);
}
