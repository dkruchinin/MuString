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
 * (c) Copyright 2009 Dan Kruchinin <dan.kruchinin@gmail.com>
 *
 * eza/amd64/faults/mem_faults.c: contains routines for dealing with
 *   memory-related x86_64 CPU fauls.
 *
 */

#include <eza/arch/types.h>
#include <eza/arch/page.h>
#include <eza/arch/fault.h>
#include <eza/arch/interrupt.h>
#include <mm/vmm.h>
#include <mm/page.h>
#include <eza/kernel.h>
#include <mlibc/kprintf.h>
#include <eza/arch/mm.h>
#include <eza/smp.h>
#include <eza/kconsole.h>
#include <eza/arch/context.h>
#include <eza/signal.h>

#define PFAULT_NP(errcode) (((errcode) & 0x1) == 0)
#define PFAULT_PROTECT(errcode) ((errcode) & 0x01)
#define PFAULT_READ(errcode) (((errcode) & 0x02) == 0)
#define PFAULT_WRITE(errcode) ((errcode) & 0x02)
#define PFAULT_SVISOR(errcode) (((errcode) & 0x04) == 0)
#define PFAULT_USER(errcode) ((errcode) & 0x04)

#define get_fault_address(x) \
    __asm__ __volatile__( "movq %%cr2, %0" : "=r"(x) )

#define NUM_STACKWORDS 5

static bool __read_user_safe(uintptr_t addr,uintptr_t *val)
{
  uintptr_t *p;
  page_idx_t pidx = ptable_ops.vaddr2page_idx(task_get_rpd(current_task()), addr, NULL);

  if( pidx == PAGE_IDX_INVAL ) {
    return false;
  }

  p=(uintptr_t*)(pframe_id_to_virt(pidx)+(addr & PAGE_MASK));
  *val=*p;
  return true;
}

void __dump_stack(uintptr_t ustack)
{
  int i;
  uintptr_t d;

  kprintf_fault("\nTop %d words of userspace stack (RSP=%p).\n\n",
          NUM_STACKWORDS,ustack);
  for(i=0;i<NUM_STACKWORDS;i++) {
    if( __read_user_safe(ustack,&d) ) {
      kprintf_fault("  <%p>\n",d);
    } else {
      kprintf_fault("  <Invalid stack pointer>\n");
    }
    ustack += sizeof(uintptr_t);
  }
}

void invalid_tss_fault_handler_impl(interrupt_stack_frame_err_t *stack_frame)
{
    kprintf_fault( "  [!!] #Invalid TSS exception raised !\n" );
}

void stack_fault_handler_impl(interrupt_stack_frame_err_t *stack_frame)
{
    kprintf_fault( "  [!!] #Stack exception raised !\n" );
}

void segment_not_present_fault_handler_impl(interrupt_stack_frame_err_t *stack_frame)
{
    kprintf_fault( "  [!!] #Segment not present exception raised !\n" );
}

void general_protection_fault_handler_impl(interrupt_stack_frame_err_t *stack_frame)
{
  regs_t *regs=(regs_t *)(((uintptr_t)stack_frame)-sizeof(struct __gpr_regs)-8);

  if (!default_console()->is_enabled)
    default_console()->enable();

  if( kernel_fault(stack_frame) ) {
    goto kernel_fault;
  }

  kprintf_fault("[CPU %d] Unhandled user-mode GPF exception! Stopping CPU with error code=%d.\n\n",
          cpu_id(), stack_frame->error_code);
  goto stop_cpu;

kernel_fault:
  kprintf_fault("[CPU %d] Unhandled kernel-mode GPF exception! Stopping CPU with error code=%d.\n\n",
          cpu_id(), stack_frame->error_code);
stop_cpu:  
  fault_dump_regs(regs,stack_frame->rip);
  show_stack_trace(stack_frame->old_rsp);
#ifdef CONFIG_DUMP_USTACK
  if (!kernel_fault(stack_frame))
    __dump_user_stack(stack_frame->old_rsp);
#endif
  interrupts_disable();
  for(;;);
}

void page_fault_fault_handler_impl(interrupt_stack_frame_err_t *stack_frame)
{
  uint64_t invalid_address,fixup;
  regs_t *regs=(regs_t *)(((uintptr_t)stack_frame)-sizeof(struct __gpr_regs)-8);
  usiginfo_t siginfo;
  task_t *faulter=current_task();

  get_fault_address(invalid_address);
  fixup = fixup_fault_address(stack_frame->rip);
  if(PFAULT_SVISOR(stack_frame->error_code) && !fixup) {
    goto kernel_fault;
  }
  else {
    /*
     * PF in user-space or "fixuped" fault in kernel.
     * Try to find out correspondig VM range and handle the faut using range's memory object.
     */

    vmm_t *vmm = current_task()->task_mm;
    uint32_t errmask = 0;
    int ret = -EFAULT;

    if (!PFAULT_READ(stack_frame->error_code))
      errmask |= PFLT_WRITE;
    if (PFAULT_PROTECT(stack_frame->error_code))
      errmask |= PFLT_PROTECT;
    else
      errmask |= PFLT_NOT_PRESENT;

    ret = vmm_handle_page_fault(vmm, invalid_address, errmask);
    if (!ret) {
      return;
    }

    if (fixup != 0) {
      goto kernel_fault;
    }
  }

#ifdef CONFIG_SEND_SIGSEGV_ON_FAULTS
  goto send_sigsegv;
#endif

  kprintf_fault("[CPU %d] Unhandled user-mode PF exception! Stopping CPU with error code=%d.\n\n",
                cpu_id(), stack_frame->error_code);
  goto stop_cpu;

kernel_fault:
  /* First, try to fix this exception. */
  if( fixup != 0 ) {
    stack_frame->rip=fixup;
    return;
  }

  kprintf_fault("[CPU %d] Unhandled kernel-mode PF exception! Stopping CPU with error code=%d.\n\n",
          cpu_id(), stack_frame->error_code);  
stop_cpu:
  fault_dump_regs(regs,stack_frame->rip);
  kprintf_fault( " Invalid address: %p\n", invalid_address );
  kprintf_fault( " RSP: %p\n", stack_frame->old_rsp);

  if( kernel_fault(stack_frame) ) {
    show_stack_trace(stack_frame->old_rsp);
  }
#ifdef CONFIG_DUMP_USPACE_STACK
  else {
    __dump_stack(stack_frame->old_rsp);
  }
#endif /* CONFIG_DUMP_USTACK */

  interrupts_disable();
  for(;;);

send_sigsegv:
  /* Send user the SIGSEGV signal. */
  INIT_USIGINFO_CURR(&siginfo);
  siginfo.si_signo=SIGSEGV;
  siginfo.si_code=SEGV_MAPERR;
  siginfo.si_addr=(void *)invalid_address;

  kprintf_fault("[!!] Sending SIGSEGV to %d:%d ('%s')\n",
                faulter->pid,faulter->tid,faulter->short_name);
  send_task_siginfo(faulter,&siginfo,true,NULL);
}

