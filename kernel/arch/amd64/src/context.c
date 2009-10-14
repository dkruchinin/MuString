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
 * (c) Copyright 2009 Dan Kruchinin <dk@jarios.org>
 */

#include <config.h>
#include <arch/context.h>
#include <mm/vmm.h>
#include <mstring/kprintf.h>
#include <mstring/task.h>
#include <mstring/usercopy.h>
#include <mstring/types.h>

void dump_gpregs(struct gpregs *gprs)
{
  kprintf_fault("  [=== General purpose registers dump ===]\n");
  kprintf_fault("  |RAX: %#.16lx | RBP: %#.16lx | RSI: %#.16lx|\n",
                gprs->rax, gprs->rbp, gprs->rsi);
  kprintf_fault("  |RDI: %#.16lx | RDX: %#.16lx | RCX: %#.16lx|\n",
                gprs->rdi, gprs->rdx, gprs->rcx);
  kprintf_fault("  |RBX: %#.16lx | R15: %#.16lx | R14: %#.16lx|\n",
                gprs->rbx, gprs->r15, gprs->r14);
  kprintf_fault("  |R13: %#.16lx | R12: %#.16lx | R11: %#.16lx|\n",
                gprs->r13, gprs->r12, gprs->r11);
  kprintf_fault("  |R10: %#.16lx | R9:  %#.16lx | R8:  %#.16lx|\n",
                gprs->r10, gprs->r9, gprs->r8);
}

void dump_interrupt_stack(struct intr_stack_frame *istack_frame)
{
  kprintf_fault("  [=== Interrupt stack frame ===]\n");
  kprintf_fault("   > RIP: %#.16lx, CS: %#.16lx\n",
                istack_frame->rip, istack_frame->cs);
  kprintf_fault("   > RSP: %#.16lx, SS: %#.16lx\n",
                istack_frame->rsp, istack_frame->cs);
  kprintf_fault("   > RFLAGS: %#.16lx\n", istack_frame->rflags);
}

#define STACKWORDS_IN_LINE 3

void dump_user_stack(vmm_t *vmm, uintptr_t rsp)
{
  int i;
  vmrange_t *vmr;
  uintptr_t cur_addr = rsp, tmp;

  vmr = vmrange_find(vmm, rsp, PAGE_ALIGN(rsp), NULL);
  preempt_disable();
  kprintf_fault("  [=== USER STACK DUMP ===]\n");
  if (unlikely(!vmr)) {
    kprintf_fault("  CORRUPTED USER STACK: %p\n", rsp);
    goto out;
  }

  kprintf_fault("    ");
  for (i = 1; i <= CONFIG_NUM_STACKWORDS; i++) {
    if (cur_addr <= vmr->bounds.space_start) {
      break;
    }
    if (copy_from_user((void *)&cur_addr, &tmp, sizeof(tmp))) {
      break;
    }

    kprintf_fault("[%#.16lx] ", tmp);
    cur_addr -= sizeof(ulong_t);
    if (!(i % STACKWORDS_IN_LINE)) {
      kprintf_fault("\n    ");
    }
  }
  if (i % STACKWORDS_IN_LINE) {
    kprintf_fault("\n");
  }

out:
  preempt_enable();
  return;
}

void dump_kernel_stack(task_t *task, uintptr_t rsp)
{
  int i;
  uintptr_t addr = rsp;

  interrupts_disable();
  kprintf_fault("  [=== KERNEL STACK DUMP ===]\n");
  if (unlikely((rsp - 8) < task->kernel_stack.low_address)) {
    kprintf("   CORRUPTED KERNEL STACK\n");
    goto out;
  }

  kprintf_fault("    ");
  for (i = 1; i <= CONFIG_NUM_STACKWORDS; i++) {
    if (rsp < task->kernel_stack.low_address) {
      break;
    }

    kprintf_fault("[%#.16lx] ", *(ulong_t *)addr);
    addr -= sizeof(ulong_t);
    if (!(i % STACKWORDS_IN_LINE)) {
      kprintf_fault("\n    ");
    }
  }
  if (i % STACKWORDS_IN_LINE) {
    kprintf_fault("\n");
  }

out:
  interrupts_enable();
}

void __stop_cpu(void)
{
  interrupts_disable();
  for (;;);
}
