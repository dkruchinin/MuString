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
 * eza/amd64/faults/mem_faults.c: contains routines for dealing with
 *   memory-related x86_64 CPU fauls.
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

#define get_fault_address(x) \
    __asm__ __volatile__( "movq %%cr2, %0" : "=r"(x) )

void invalid_tss_fault_handler_impl(interrupt_stack_frame_err_t *stack_frame)
{
    kprintf( "  [!!] #Invalid TSS exception raised !\n" );
}

void stack_fault_handler_impl(interrupt_stack_frame_err_t *stack_frame)
{
    kprintf( "  [!!] #Stack exception raised !\n" );
}

void segment_not_present_fault_handler_impl(interrupt_stack_frame_err_t *stack_frame)
{
    kprintf( "  [!!] #Segment not present exception raised !\n" );
}

void general_protection_fault_handler_impl(interrupt_stack_frame_err_t *stack_frame)
{
  if( kernel_fault(stack_frame) ) {
    kprintf( "#GPF in kernel mode: RIP = 0x%X\n", stack_frame->rip );    
  }

  kprintf( "[!!!] Unhandled GPF exception ! RIP: %p (CODE: %d) ...\n",
           stack_frame->rip, stack_frame->error_code );
  l1: goto l1;
}

static void __dump_regs(char *sp)
{
  regs_t *r=(regs_t *)sp;
   kprintf("rax=%p,rdi=%p,rsi=%p\nrdx=%p,rcx=%p\n",
	   r->rax,r->rdi,r->rsi,r->rdx,r->rcx);
   return;
}


void page_fault_fault_handler_impl(interrupt_stack_frame_err_t *stack_frame)
{
  uint64_t invalid_address,fixup;
char *sp=(char *)stack_frame-sizeof(regs_t);
   
  get_fault_address(invalid_address);
  if( kernel_fault(stack_frame) ) {
    goto kernel_fault;
  }

  kprintf( "[!!!] Unhandled user PF exception ! Stopping (CODE: %d, See page 225). Address: %p\nrip=%p\n",
           stack_frame->error_code, invalid_address,stack_frame->rip);
   __dump_regs(sp);

  l2: goto l2;

kernel_fault:
  /* First, try to fix this exception. */
  fixup=fixup_fault_address(stack_frame->rip);
  if( fixup != 0 ) {
    stack_frame->rip=fixup;
    goto out;
  }

  kprintf( "[!!!] Unhandled kernel PF exception ! Stopping (CODE: %d, See page 225). Address: %p\n",
           stack_frame->error_code, invalid_address);
  l1: goto l1;
out:
  return;
}

