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

#define XMM_CTX_SIZE  512

#define USERSPACE_SIGNAL_INVOKER  0x1003000
#define USERSPACE_SIGNAL_INVOKER_INT  0x1003000

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

static status_t __setup_trampoline_ctx(struct __signal_context * __user ctx,
                                       siginfo_t *siginfo,sa_sigaction_t act)
{
  struct __trampoline_ctx kt;

  if( copy_to_user(&ctx->siginfo,siginfo,sizeof(*siginfo) ) ) {
    return -EFAULT;
  }

  kt.handler=(uint64_t)act;
  kt.arg1=siginfo->si_signo;
  kt.arg2=(uint64_t)siginfo;
  kt.arg3=0;

  if( copy_to_user(&ctx->trampl_ctx,&kt,sizeof(kt)) ) {
    return -EFAULT;
  }

  return 0;
}

static void __perform_default_action(int sig)
{
  kprintf( ">>>>>>> DEFAULT ACTION FOR %d\n",sig );
}

static void __handle_pending_signals(int reason, uint64_t retcode,
                                     uintptr_t kstack)
{
  uintptr_t ustack,rcx;
  struct __signal_context *ctx;
  sigq_item_t *sigitem;
  struct __gpr_regs *kpregs;
  sa_sigaction_t act;
  task_t *caller=current_task();
  struct __int_stackframe *int_frame;

  sigitem=extract_one_signal_from_queue(current_task());
  if( !sigitem ) {
    panic("No signals for delivery for %d/%d !\n",
          current_task()->pid,current_task()->tid);
  }

  /* Determine what is there a user-specific handler installed ? */
  LOCK_TASK_STRUCT(caller);
  act=caller->siginfo.handlers->actions[sigitem->info.si_signo].a.sa_sigaction;
  UNLOCK_TASK_STRUCT(caller);

  if( act == SIG_IGN ) {
    /* Someone has suddenly disabled the signal. */
    goto out;
  } else if( act == SIG_DFL ) {
    /* Perform default action. */
    __perform_default_action(sigitem->info.si_signo);
  } else {
    /* Perform user-specific action. First, save signal context in user stack.*/
    ustack=get_userspace_stack_pointer();

    /* Allocate signal context using user's stack area. */
    ctx=(struct __signal_context *)(ustack-sizeof(struct __signal_context));

    if( __setup_general_ctx(&ctx->gen_ctx,kstack,&kpregs) ) {
      goto bad_memory;
    }

    if( __setup_trampoline_ctx(ctx,&sigitem->info,act) ) {
      goto bad_memory;
    }

    if( copy_to_user(&ctx->retcode,&retcode,sizeof(retcode)) ) {
      goto bad_memory;
    }

    /* Update user's stack to point at our context frame. */
    set_userspace_stack_pointer(ctx);

    /* OK, it's time to setup return addresses to invoke the handler. */
    switch( reason ) {
      case __SYCALL_UWORK:
        kpregs->rcx=USERSPACE_SIGNAL_INVOKER;
        break;
      case __INT_UWORK:
        ustack=(uintptr_t)(kpregs++);
        /* OK, now we're pointing at saved interrupt number (see asm.S).
         * So skip it.
         */
        ustack += 8;
        /* Now we can access hardware interrupt stackframe. */
        int_frame=( struct __int_stackframe *)ustack;
        int_frame->rip=USERSPACE_SIGNAL_INVOKER_INT;
        break;
      default:
        panic( "Unknown userspace work type: %d in task (%d:%d)\n",
               reason,caller->pid,caller->tid);
    }

    /* We must apply the mask of blocked signals according to user's settings */
  }

  clear_task_signals_pending(current_task());
  free_sigqueue_item(sigitem);
out:
  return;
bad_memory:
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
  for(;;);
  return -1;
}
