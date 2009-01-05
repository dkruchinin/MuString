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
#include <mm/mmap.h>
#include <eza/signal.h>

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

static int __send_sigsegv_on_faults=1;

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

#define NUM_STACKWORDS 5

static bool __read_user_safe(uintptr_t addr,uintptr_t *val)
{
  uintptr_t *p;
  page_idx_t pidx=mm_pin_virt_addr(current_task()->page_dir,addr);

  if( pidx < 0 ) {
    return false;
  }

  p=(uintptr_t*)(pframe_id_to_virt(pidx)+(addr & PAGE_OFFSET_MASK));
  *val=*p;
  return true;
}

void __dump_stack(uintptr_t ustack)
{
  int i;
  uintptr_t d;

  kprintf("\nTop %d words of userspace stack (RSP=%p).\n\n",
          NUM_STACKWORDS,ustack);
  for(i=0;i<NUM_STACKWORDS;i++) {
    if( __read_user_safe(ustack,&d) ) {
      kprintf("  <%p>\n",d);
    } else {
      kprintf("  <Invalid stack pointer>\n");
    }
    ustack += sizeof(uintptr_t);
  }
}

void page_fault_fault_handler_impl(interrupt_stack_frame_err_t *stack_frame)
{
  uint64_t invalid_address,fixup;
  regs_t *regs=(regs_t *)(((uintptr_t)stack_frame)-sizeof(struct __gpr_regs)-8);
  siginfo_t siginfo;
  task_t *faulter=current_task();

  get_fault_address(invalid_address);

  if( kernel_fault(stack_frame) )
    goto kernel_fault;

  if( __send_sigsegv_on_faults )
    goto send_sigsegv;

  kprintf("[CPU %d] Unhandled user-mode PF exception! Stopping CPU with error code=%d.\n\n",
          cpu_id(), stack_frame->error_code);
  goto stop_cpu;

kernel_fault:
  /* First, try to fix this exception. */
  fixup=fixup_fault_address(stack_frame->rip);
  if( fixup != 0 ) {
    stack_frame->rip=fixup;
    return;
  }

  kprintf("[CPU %d] Unhandled kernel-mode PF exception! Stopping CPU with error code=%d.\n\n",
          cpu_id(), stack_frame->error_code);
stop_cpu:
  __dump_regs(regs,stack_frame->rip);
  kprintf( " Invalid address: %p\n", invalid_address );

  if( !kernel_fault(stack_frame) ) {
    __dump_stack(stack_frame->old_rsp);
  }

  interrupts_disable();
  for(;;);

send_sigsegv:
  /* Send user the SIGSEGV signal. */
  INIT_SIGINFO_CURR(&siginfo);
  siginfo.si_signo=SIGSEGV;
  siginfo.si_code=SEGV_MAPERR;
  siginfo.si_addr=(void *)invalid_address;

  kprintf( "[F]: Sending SIGSEGV.\n" );
  send_task_siginfo(faulter,&siginfo,true);
  kprintf( "[F]: Done !\n" );
}

