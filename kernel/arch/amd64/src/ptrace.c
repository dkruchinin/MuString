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
 * (c) Copyright 2009,2010 Dmitry Gromada <gromada@jarios.org>
 *
 * amd64 arch depended part of the process trace functionality
 */

#include <mstring/process.h>
#include <mstring/ptrace.h>
#include <arch/ptrace.h>
#include <arch/context.h>
#include <arch/cpu.h>
#include <mstring/usercopy.h>

static struct gpregs *get_task_gpregs(task_t *child, regs_t *regs)
{
  struct gpregs *gpr = &regs->gpr_regs;

  /*
   * Если отлаживаемый поток попал в ядро в результате исключения, то
   * регистры общего назначения располагаются выше на размер
   * кода возврата из из исключения
   */
  if (check_task_flags(child, TF_INFAULT))
    gpr = (struct gpregs*)((uintptr_t)gpr - sizeof(long));

  return gpr;
}

static void pull_int_regs(task_t *child, struct ptrace_regs *pt_regs)
{
  struct gpregs *gpr;
  struct intr_stack_frame *frame;
  arch_context_t *ctx;

  regs_t *kregs = (regs_t*)(child->kernel_stack.high_address -
                            sizeof(regs_t));

  gpr = get_task_gpregs(child, kregs);
  frame = &kregs->int_frame;
  ctx = (arch_context_t*)child->arch_context;

  pt_regs->rax = gpr->rax;
  pt_regs->rbx = gpr->rbx;
  pt_regs->rcx = gpr->rcx;
  pt_regs->rdx = gpr->rdx;
  pt_regs->rsi = gpr->rsi;
  pt_regs->rdi = gpr->rdi;
  pt_regs->rbp = gpr->rbp;
  pt_regs->rsp = frame->rsp;
  pt_regs->r8 = gpr->r8;
  pt_regs->r9 = gpr->r9;
  pt_regs->r10 = gpr->r10;
  pt_regs->r11 = gpr->r11;
  pt_regs->r12 = gpr->r12;
  pt_regs->r13 = gpr->r13;
  pt_regs->r14 = gpr->r14;
  pt_regs->r15 = gpr->r15;
  pt_regs->rip = frame->rip;
  pt_regs->eflags = frame->rflags;
  pt_regs->cs = frame->cs;
  pt_regs->ss = frame->ss;
  pt_regs->ds = ctx->ds;
  pt_regs->es = ctx->es;
  pt_regs->fs = ctx->fs;
  pt_regs->gs = ctx->gs;
}

static void push_int_regs(task_t *child, struct ptrace_regs *pt_regs)
{
  struct gpregs *gpr;
  struct intr_stack_frame *frame;

  regs_t *kregs = (regs_t*)(child->kernel_stack.high_address -
                            sizeof(regs_t));

  gpr = get_task_gpregs(child, kregs);
  frame = &kregs->int_frame;

  gpr->rax = pt_regs->rax;
  gpr->rbx = pt_regs->rbx;
  gpr->rcx = pt_regs->rcx;
  gpr->rdx = pt_regs->rdx;
  gpr->rsi = pt_regs->rsi;
  gpr->rdi = pt_regs->rdi;
  gpr->rbp = pt_regs->rbp;
  frame->rsp = pt_regs->rsp;
  gpr->r8 = pt_regs->r8;
  gpr->r9 = pt_regs->r9;
  gpr->r10 = pt_regs->r10;
  gpr->r11 = pt_regs->r11;
  gpr->r12 = pt_regs->r12;
  gpr->r13 = pt_regs->r13;
  gpr->r14 = pt_regs->r14;
  gpr->r15 = pt_regs->r15;

  /* write to other registers is denied */
}

int arch_ptrace(ptrace_cmd_t cmd, task_t *child, uintptr_t addr, void *data)
{
  int ret = 0;
  struct ptrace_regs pt_regs;

  /*
   * get/set arch specific context under lock
   * so that to avoid concurrent task resuming
   */

  if (cmd == PTRACE_WRITE_INT_REGS || cmd == PTRACE_WRITE_FLP_REGS) {
    ret = copy_from_user(&pt_regs, data, sizeof(struct ptrace_regs));
    if (ret)
      return ERR(ret);
  }

  LOCK_TASK_STRUCT(child);

  if (check_task_flags(child, TF_DISINTEGRATING)) {
    ret = -EPERM;
    goto out;
  }

  if (!task_stopped(child)) {
    ret = -ESRCH;
    goto out;
  }

  switch (cmd) {
  case PTRACE_READ_INT_REGS:
    pull_int_regs(child, &pt_regs);
    break;
  case PTRACE_WRITE_INT_REGS:
    push_int_regs(child, &pt_regs);
    break;
  case PTRACE_READ_FLP_REGS:
    ret = -ENOSYS;
    break;
  case PTRACE_WRITE_FLP_REGS:
    ret = -ENOSYS;
    break;
  case PTRACE_SINGLE_STEP:
    set_task_flags(child, TF_SINGLE_STEP);
    break;
  default:
    ret = -EINVAL;
    break;
  }

out:
  UNLOCK_TASK_STRUCT(child);

  if (!ret && (cmd == PTRACE_READ_INT_REGS || cmd == PTRACE_READ_FLP_REGS))
    ret = copy_to_user(data, &pt_regs, sizeof(struct ptrace_regs));

  return ERR(ret);
}

void arch_ptrace_cont(task_t *child)
{
  regs_t *regs = (regs_t*)(child->kernel_stack.high_address -
                           sizeof(regs_t));

  /*
   * Разрешить/запретить пошаговый режим путём устновки/сброса
   * TF бита в регистре rFLAGS (см. AMD developer manual vol.2)
   */
  if (check_task_flags(child, TF_SINGLE_STEP))
    regs->int_frame.rflags |= RFLAGS_TF;
  else
    regs->int_frame.rflags &= ~RFLAGS_TF;
}

