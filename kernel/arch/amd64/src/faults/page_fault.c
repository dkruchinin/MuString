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
 */

#include <config.h>
#include <arch/seg.h>
#include <arch/fault.h>
#include <arch/context.h>
#include <arch/cpu.h>
#include <mm/vmm.h>
#include <mstring/kprintf.h>
#include <mstring/task.h>
#include <mstring/signal.h>
#include <mstring/types.h>

static inline void display_unhandled_pf_info(struct fault_ctx *fctx, uintptr_t fault_addr)
{
  fault_describe("PAGE FAULT", fctx);
  kprintf_fault("  Invalid address: %p, RIP: %p\n",
                fault_addr, fctx->istack_frame->rip);
  fault_dump_info(fctx);
}

#ifdef CONFIG_SEND_SIGSEGV_ON_FAULTS
static void send_sigsegv(uintptr_t fault_addr)
{  
  usiginfo_t siginfo;

  /* Send user the SIGSEGV signal. */
  INIT_USIGINFO_CURR(&siginfo);
  siginfo.si_signo=SIGSEGV;
  siginfo.si_code=SEGV_MAPERR;
  siginfo.si_addr=(void *)fault_addr;

  kprintf_fault("[!!] Sending SIGSEGV to %d:%d ('%s')\n",
                current_task()->pid, current_task()->tid,
                current_task()->short_name);
  send_task_siginfo(current_task(), &siginfo, true, NULL);
}
#endif /* CONFIG_SEND_SIGSEGV_ON_FAULTS */

struct fixup_record {
  uint64_t fault_address, fixup_address;
}; 

extern int __ex_table_start, __ex_table_end;

static uint64_t fixup_fault_address(uint64_t fault_address)
{
  struct fixup_record *fr = (struct fixup_record *)&__ex_table_start;
  struct fixup_record *end = (struct fixup_record *)&__ex_table_end;

  while (fr<end) {
    if (fr->fault_address == fault_address) {
      return fr->fixup_address;
    }

    fr++;
  }

  return 0;
}

#define PFAULT_NP(errcode)      (((errcode) & 0x1) == 0)
#define PFAULT_PROTECT(errcode) ((errcode) & 0x01)
#define PFAULT_READ(errcode)    (((errcode) & 0x02) == 0)
#define PFAULT_WRITE(errcode)   ((errcode) & 0x02)
#define PFAULT_SVISOR(errcode)  (((errcode) & 0x04) == 0)
#define PFAULT_USER(errcode)    ((errcode) & 0x04)
#define PFAULT_NXE(errcode)     ((errcode) & 0x10)

void FH_page_fault(struct fault_ctx *fctx)
{
  uintptr_t fault_addr, fixup_addr;
  struct intr_stack_frame *stack_frame = fctx->istack_frame;

  fault_addr = read_cr2();
  fixup_addr = fixup_fault_address(stack_frame->rip);    
  if (IS_KERNEL_FAULT(fctx) && !fixup_addr) {
    display_unhandled_pf_info(fctx, fault_addr);
    goto stop_cpu;
  }
  else {
    vmm_t *vmm = current_task()->task_mm;
    uint32_t errmask = 0;
    int ret = -EFAULT;

    if (!PFAULT_READ(fctx->errcode))
      errmask |= PFLT_WRITE;
    if (PFAULT_NXE(fctx->errcode))
      errmask |= PFLT_NOEXEC;
    if (PFAULT_PROTECT(fctx->errcode))
      errmask |= PFLT_PROTECT;
    else
      errmask |= PFLT_NOT_PRESENT;

    ret = vmm_handle_page_fault(vmm, fault_addr, errmask);
    if (!ret) {
      return;
    }
    
    display_unhandled_pf_info(fctx, fault_addr);
#ifdef CONFIG_SEND_SIGSEGV_ON_FAULTS
    send_sigsegv(fault_addr);
    return;
#endif /* CONFIG_SEND_SIGSEGV_ON_FAULTS */
  }
  if (fixup_addr) {
      kprintf("XXX FAULT %p, FIXUP %p\n",
              fault_addr, fixup_addr);
    stack_frame->rip = fixup_addr;
    return;
  }

stop_cpu:
  __stop_cpu();
}

