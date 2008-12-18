#include <eza/arch/types.h>
#include <mlibc/kprintf.h>
#include <eza/smp.h>
#include <eza/task.h>
#include <eza/arch/context.h>
#include <eza/signal.h>
#include <kernel/vm.h>
#include <eza/errno.h>

#define XMM_CTX_SIZE  512

#define USERSPACE_SIGNAL_INVOKER  0x1003000

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

static void __setup_retaddr_syscall(struct __gpr_regs *gpr)
{
  gpr->rcx=USERSPACE_SIGNAL_INVOKER;
}

static void __setup_retaddr_int(struct __gpr_regs *gpr)
{
}

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

static void __handle_pending_signals(int reason, uint64_t retcode,
                                     uintptr_t kstack)
{
  uint64_t ustack;
  struct __signal_context *ctx;
  sigq_item_t *sigitem;
  struct __gpr_regs *kpregs;
  sa_sigaction_t act;
  task_t *caller=current_task();

  sigitem=extract_one_signal_from_queue(current_task());
  if( !sigitem ) {
    panic("No signals for delivery for %d/%d !\n",
          current_task()->pid,current_task()->tid);
  }

  /* Determine what is there a user-specific handler installed ? */
  LOCK_TASK_STRUCT(caller);
  act=caller->siginfo.handlers->actions[sigitem->info.si_signo].a.sa_sigaction;
  UNLOCK_TASK_STRUCT(caller);

  kprintf( "=====> SIGNAL ACTION: 0x%X\n",act );
  if( act == SIG_IGN ) { /* Someone has suddenly disabled the signal. */

    return;
  } else if( act == SIG_DFL ) { /* Perform default action. */
    kprintf( ">>>>>>> DEFAULT ACTION FOR %d\n",sigitem->info.si_signo );

  } else { /* Perform user-specific action. */
    /* Now we can save signal context in user stack. So read the top
     of userspace stack. */
    __asm__ __volatile__( "movq %%gs:(%1), %0"
                          :"=r"(ustack)
                          : "R"((uint64_t)CPU_SCHED_STAT_USTACK_OFFT),
                            "R"((uint64_t)0) );

    /* Allocate signal context using user's stack area. */
    ctx=(struct __signal_context *)(ustack-sizeof(struct __signal_context));

    if( __setup_general_ctx(&ctx->gen_ctx,kstack,&kpregs) ) {
      goto bad_memory;
    }
    kprintf( "GENERAL CTX CREATED.\n" );

    if( __setup_trampoline_ctx(ctx,&sigitem->info,act) ) {
      goto bad_memory;
    }
    kprintf( "TRAMPLONE CTX CREATED.\n" );

    if( copy_to_user(&ctx->retcode,&retcode,sizeof(retcode)) ) {
      goto bad_memory;
    }
    kprintf( "RETCODE SAVED.\n" );

    /* Update user's stack to point at our context frame. */
    __asm__ __volatile__( "movq %0, %%gs:(%1)"
                          :: "R"(ctx), "R"((uint64_t)CPU_SCHED_STAT_USTACK_OFFT) );  

    /* OK, it's time to setup return addresses to invoke the handler. */
    switch( reason ) {
      case __SYCALL_UWORK:
        __setup_retaddr_syscall(kpregs);
        kprintf( "RETADDR UPDATED.\n" );
        break;
      default:
        __setup_retaddr_int(kpregs);
        break;
    }
  }

  clear_task_signals_pending(current_task());
  free_sigqueue_item(sigitem);
  kprintf("[UWORKS]: %d/%d. Processing works for %d:%d, STACK: %p, KSTACK: %p\n",
          reason,retcode,
          current_task()->pid,current_task()->tid,
          ustack,kstack);
  kprintf("[UWORKS]: Pending signals: 0x%X\n",
          current_task()->siginfo.pending);
bad_memory:
  return;
}


void handle_uworks(int reason, uint64_t retcode,uintptr_t kstack)
{
  __handle_pending_signals(reason,retcode,kstack);
}

status_t sys_sigreturn(void *ctx) {
  return 0;
}
