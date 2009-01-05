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
 * eza/arch/amd64/signal.c: AMD64-specific code for signal delivery.
 */

#include <eza/arch/types.h>
#include <mlibc/kprintf.h>
#include <eza/smp.h>
#include <eza/task.h>
#include <eza/arch/context.h>
#include <eza/signal.h>
#include <kernel/vm.h>
#include <eza/errno.h>
#include <eza/arch/current.h>
#include <kernel/vm.h>
#include <eza/process.h>

#define XMM_CTX_SIZE  512

#define USPACE_TRMPL(a) USPACE_ADDR((a),UTRAMPOLINE_VIRT_ADDR)

struct __trampoline_ctx {
  uint64_t handler,arg1,arg2,arg3;
};

struct __gen_ctx {
  uint8_t xmm[XMM_CTX_SIZE];
  struct __gpr_regs gpr_regs;
};

struct __signal_context {
  struct __trampoline_ctx trampl_ctx;
  struct __gen_ctx gen_ctx;
  siginfo_t siginfo;
  uint64_t retcode;
};

struct __extra_int_ctx {
  uintptr_t rcx,retaddr;
};

/* Userspace trampolines */
extern void trampoline_sighandler_invoker_sys(void);
extern void trampoline_sighandler_invoker_int(void);
extern void trampoline_sighandler_invoker_int_bottom(void);

static status_t __setup_general_ctx(struct __gen_ctx * __user ctx,
                                    uintptr_t kstack,
                                    struct __gpr_regs **kpregs)
{
  uint8_t *p;

  /* Save XMM and GPR context. */
  p=(uint8_t *)kstack+8;  /* Skip pointer to saved GPRs. */
  if( copy_to_user(ctx->xmm,p,XMM_CTX_SIZE) ) {
    return -EFAULT;
  }

  /* Locate and save GPRs. */
  p=(uint8_t*)( *(uintptr_t *)kstack );
  if( copy_to_user( &ctx->gpr_regs,p,sizeof(struct __gpr_regs) ) ) {
    return -EFAULT;
  }

  *kpregs=(struct __gpr_regs *)p;
  return 0;
}

static status_t __setup_trampoline_ctx(struct __signal_context *__user ctx,
                                       siginfo_t *siginfo,sa_sigaction_t act)
{
  struct __trampoline_ctx kt;

  if( copy_to_user(&ctx->siginfo,siginfo,sizeof(*siginfo) ) ) {
    return -EFAULT;
  }

  kt.handler=(uint64_t)act;
  kt.arg1=siginfo->si_signo;
  kt.arg2=(uint64_t)&ctx->siginfo;
  kt.arg3=0;

  if( copy_to_user(&ctx->trampl_ctx.handler,&kt,sizeof(kt)) ) {
    return -EFAULT;
  }

  kprintf( "TRAMPLCTX: %p, handler: %p, arg1=%p,arg2=%p,arg3=%p\n",
           &ctx->trampl_ctx,
           ctx->trampl_ctx.handler,ctx->trampl_ctx.arg1,
           ctx->trampl_ctx.arg2,ctx->trampl_ctx.arg3);

  return 0;
}

static void __perform_default_action(int sig)
{
  int sm=_BM(sig);

  kprintf( ">>>>>>> DEFAULT ACTION FOR %d: ",sig );
  if( sm & LETHAL_SIGNALS ) {
    kprintf( "TERMINATE PROCESS\n" );
    do_exit(EXITCODE(sig,0));
  }
  kprintf( "IGNORE\n" );
  for(;;);
}

static status_t __setup_syscall_context(uint64_t retcode,uintptr_t kstack,
                                        siginfo_t *info,sa_sigaction_t act)
{
  uintptr_t ustack;
  struct __signal_context *ctx;
  struct __gpr_regs *kpregs;

  /* Perform user-specific action. First, save signal context in user stack.*/
  ustack=get_userspace_stack_pointer();

  /* Allocate signal context using user's stack area. */
  ctx=(struct __signal_context *)(ustack-sizeof(struct __signal_context));

  if( __setup_general_ctx(&ctx->gen_ctx,kstack,&kpregs) ) {
    return -EFAULT;
  }

  if( __setup_trampoline_ctx(ctx,info,act) ) {
    return -EFAULT;
  }

  if( copy_to_user(&ctx->retcode,&retcode,sizeof(retcode)) ) {
    return -EFAULT;
  }

  /* Update user's stack to point at our context frame. */
  set_userspace_stack_pointer(ctx);

  /* Setup trampoline. */
  kpregs->rcx=USPACE_TRMPL(trampoline_sighandler_invoker_sys);
  return 0;
}

static status_t __setup_int_context(uint64_t retcode,uintptr_t kstack,
                                    siginfo_t *info,sa_sigaction_t act,
                                    ulong_t extra_bytes)
{
  uintptr_t ustack;
  struct __extra_int_ctx *int_ctx,kint_ctx;
  struct __int_stackframe *int_frame;
  struct __gpr_regs *kpregs;
  struct __signal_context *ctx;
  uint8_t *xmm;
  
  /* Save XMM and GPR context. */
  xmm=(uint8_t *)kstack+8;  /* Skip pointer to saved GPRs. */

  /* Locate saved GPRs. */
  //kpregs=(struct __gpr_regs *)(xmm+XMM_CTX_SIZE);
  kpregs=(struct __gpr_regs *)(*(uintptr_t *)kstack);

  kstack=(uintptr_t)kpregs + sizeof(*kpregs);
  /* OK, now we're pointing at saved interrupt number (see asm.S).
   * So skip it.
   */
  kstack += (8+extra_bytes);

  /* Now we can access hardware interrupt stackframe. */
  int_frame=(struct __int_stackframe *)kstack;

  /* Save address where to continue execution from. */
  kint_ctx.retaddr=int_frame->rip;
  /* Copy saved %rcx to copy it userspace context later. */
  kint_ctx.rcx=kpregs->rcx;

  /* %rcx will specify the address to continue execution from, so
   * make it point to a valid routine.
   */
  kpregs->rcx=USPACE_TRMPL(trampoline_sighandler_invoker_int_bottom);

  /* Setup trampoline. */
  int_frame->rip=USPACE_TRMPL(trampoline_sighandler_invoker_int);

  /* Now we can create signal context. */
  ctx=(struct __signal_context *)(int_frame->old_rsp-sizeof(*ctx)-sizeof(*int_ctx));

  if( copy_to_user(&ctx->gen_ctx.xmm,xmm,XMM_CTX_SIZE) ) {
    return -EFAULT;
  }

  if( copy_to_user(&ctx->gen_ctx.gpr_regs,kpregs,sizeof(*kpregs)) ) {
    return -EFAULT;
  }

  if( __setup_trampoline_ctx(ctx,info,act) ) {
    return -EFAULT;
  }

  if( copy_to_user(&ctx->retcode,&retcode,sizeof(retcode)) ) {
    return -EFAULT;
  }

  ustack=(uintptr_t)ctx;

  /* Now copy extra interrupt context to userspace. */
  int_ctx=(struct __extra_int_ctx *)(++ctx);
  if( copy_to_user(int_ctx,&kint_ctx,sizeof(kint_ctx) ) ) {
    return -EFAULT;
  }

  /* Setup user stack pointer. */
  int_frame->old_rsp=ustack;

  return 0;
}

static void __handle_pending_signals(int reason, uint64_t retcode,
                                     uintptr_t kstack)
{
  status_t r;
  sigq_item_t *sigitem;
  sa_sigaction_t act;
  task_t *caller=current_task();

  sigitem=extract_one_signal_from_queue(caller);
  if( !sigitem ) {
    kprintf("** No signals for delivery for %d/%d !\n",
            current_task()->pid,current_task()->tid);
    for(;;);
  }

  /* Determine if there is a user-specific handler installed. */
  LOCK_TASK_SIGNALS(caller);
  act=caller->siginfo.handlers->actions[sigitem->info.si_signo].a.sa_sigaction;
  UNLOCK_TASK_SIGNALS(caller);

  if( act == SIG_IGN ) {
    goto out_recalc;
  } else if( act == SIG_DFL ) {
    __perform_default_action(sigitem->info.si_signo);
  } else {
    switch( reason ) {
      case __SYCALL_UWORK:
        r=__setup_syscall_context(retcode,kstack,&sigitem->info,act);
        break;
      case __INT_UWORK:
      case __XCPT_NOERR_UWORK:
        r=__setup_int_context(retcode,kstack,&sigitem->info,act,0);
        break;
      case __XCPT_ERR_UWORK:
        r=__setup_int_context(retcode,kstack,&sigitem->info,act,8);
        break;
      default:
        panic( "Unknown userspace work type: %d in task (%d:%d)\n",
               reason,caller->pid,caller->tid);
    }
    if( r ) {
      goto bad_memory;
    }
  }

out_recalc:
  update_pending_signals(current_task());
  free_sigqueue_item(sigitem);
  return;
bad_memory:
  kprintf( "***** BAD MEMORY !!!\n" );
  for(;;);
  return;
}

void handle_uworks(int reason, uint64_t retcode,uintptr_t kstack)
{
  kprintf("[UWORKS]: %d/%d. Processing works for %d:%d, KSTACK: %p\n",
          reason,retcode,
          current_task()->pid,current_task()->tid,
          kstack);
  kprintf("[UWORKS]: Pending signals: 0x%X\n",
          current_task()->siginfo.pending);

  __handle_pending_signals(reason,retcode,kstack);
}

status_t sys_sigreturn(uintptr_t ctx)
{
  struct __signal_context *uctx=(struct __signal_context *)ctx;
  status_t retcode;
  task_t *caller=current_task();
  uintptr_t kctx=caller->kernel_stack.high_address-sizeof(struct __gpr_regs);
  uintptr_t skctx=kctx;

  if( !valid_user_address_range(ctx,sizeof(struct __signal_context)) ) {
    goto bad_ctx;
  }

  /* Restore GPRs. */
  if( copy_from_user(kctx,&uctx->gen_ctx.gpr_regs,sizeof(struct __gpr_regs) ) ) {
    goto bad_ctx;
  }

  /* Make sure user has valid return address. */
  if( !valid_user_address(((struct __gpr_regs *)kctx)->rcx) ) {
    goto bad_ctx;
  }

  /* Area for saving XMM context must be 16-bytes aligned. */
  kctx-=XMM_CTX_SIZE;
  kctx &= 0xfffffffffffffff0;

  /* Restore XMM context. */
  if( copy_from_user(kctx,uctx->gen_ctx.xmm,XMM_CTX_SIZE) ) {
    goto bad_ctx;
  }

  /* Save location of saved GPR frame. */
  *((uintptr_t *)(kctx-sizeof(uintptr_t)))=skctx;

  /* Finally, restore retcode. */
  if( !copy_from_user(&retcode,&uctx->retcode,sizeof(retcode)) ) {
    set_userspace_stack_pointer(ctx+sizeof(struct __signal_context));
    return retcode;
  }
bad_ctx:
  kprintf("[!!!] BAD context for 'sys_sigreturn()' for task %d !\n",
          caller->tid);
  for(;;);
}
