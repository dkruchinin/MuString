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
#include <eza/kconsole.h>
#include <eza/arch/context.h>

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
  default_console()->enable();
  if( kernel_fault(stack_frame) ) {
    kprintf( "#GPF in kernel mode: RIP = 0x%X\n", stack_frame->rip );    
  }

  kprintf( "[!!!] Unhandled GPF exception ! RIP: %p (CODE: %d) ...\n",
           stack_frame->rip, stack_frame->error_code );
  l1: goto l1;
}

static void __dump_regs(regs_t *r,ulong_t rip)
{
  kprintf(" Current task: PID=%d, TID=0x%X\n",
          current_task()->pid,current_task()->tid);
  kprintf(" RAX: %p, RBX: %p, RDI: %p, RSI: %p\n RDX: %p, RCX: %p\n",
          r->rax,r->gpr_regs.rbx,
          r->gpr_regs.rdi,r->gpr_regs.rsi,
          r->gpr_regs.rdx,r->gpr_regs.rcx);
  kprintf(" RIP: %p\n",rip);
}

void page_fault_fault_handler_impl(interrupt_stack_frame_err_t *stack_frame)
{
  uint64_t invalid_address,fixup;
  regs_t *regs=(regs_t *)(((uintptr_t)stack_frame)-sizeof(struct __gpr_regs)-8);

  get_fault_address(invalid_address);

  if( kernel_fault(stack_frame) ) {
    goto kernel_fault;
  }

  default_console()->enable();
  kprintf("[CPU %d] Unhandled user-mode PF exception! Stopping CPU with error code=%d.\n",
          cpu_id(), stack_frame->error_code);
  goto send_sigsegv;

kernel_fault:
  /* First, try to fix this exception. */
  fixup=fixup_fault_address(stack_frame->rip);
  if( fixup != 0 ) {
    stack_frame->rip=fixup;
    return;
  }

  kprintf("[CPU %d] Unhandled kernel-mode PF exception! Stopping CPU with error code=%d.\n",
          cpu_id(), stack_frame->error_code);
send_sigsegv:
  __dump_regs(regs,stack_frame->rip);
  kprintf( " Invalid address: %p\n", invalid_address );

  interrupts_disable();
  for(;;);
}

