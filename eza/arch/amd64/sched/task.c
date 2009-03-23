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
 *
 * eza/amd64/sched/task.c: AMD64-specific tasks-related functions.
 */

#include <ds/iterator.h>
#include <kernel/syscalls.h>
#include <eza/task.h>
#include <eza/arch/context.h>
#include <mlibc/string.h>
#include <eza/arch/page.h>
#include <mlibc/kprintf.h>
#include <mm/page.h>
#include <mm/vmm.h>
#include <mm/pfi.h>
#include <eza/errno.h>
#include <mlibc/string.h>
#include <eza/smp.h>
#include <mm/pfalloc.h>
#include <eza/kernel.h>
#include <eza/scheduler.h>
#include <eza/arch/scheduler.h>
#include <eza/arch/current.h>
#include <eza/process.h>
#include <eza/arch/context.h>
#include <eza/arch/profile.h>
#include <eza/arch/ptable.h>
#include <config.h>

/* Located on 'amd64/asm.S' */
extern void kthread_fork_path(void);
extern void user_fork_path(void);
extern void user_fork_path_test(void);

/* Bytes enough to store our arch-specific context. */
#define ARCH_CONTEXT_BUF_SIZE  1256

/* Default kernel threads flags. */
#define KERNEL_THREAD_FLAGS  (CLONE_MM)

/* Per-CPU glabal structure that reflects the most important kernel states. */
cpu_sched_stat_t PER_CPU_VAR(cpu_sched_stat);

static void __setup_arch_segment_regs(arch_context_t *ctx,
                                      task_privelege_t priv)
{
  uint64_t fs,es,gs,ds;

  if( priv == TPL_KERNEL ) {
    fs=KERNEL_SELECTOR(KDATA_DES);
    es=fs;
    gs=fs;
    ds=fs;
  } else {
    fs=USER_SELECTOR(PTD_SELECTOR) | 0x4; /* First selector in LDT. */
    es=USER_SELECTOR(UDATA_DES);
    gs=es;
    ds=es;
  }
  ctx->fs = fs;
  ctx->es = es;
  ctx->gs = gs;
  ctx->ds = ds;
}

static void __arch_setup_ctx(task_t *newtask,uint64_t rsp,
                             task_privelege_t priv)
{
  arch_context_t *ctx = (arch_context_t*)&(newtask->arch_context[0]);

  __setup_arch_segment_regs(ctx,priv);
 
  ctx->rsp = rsp;
  /* Setup CR3 */
  ctx->cr3 = _k2p((uintptr_t)pframe_to_virt(RPD_PAGEDIR(task_get_rpd(newtask))));
  ctx->user_rsp = 0;

  /* Default TSS value which means: use per-CPU TSS. */
  ctx->tss=NULL;
  ctx->tss_limit=TSS_DEFAULT_LIMIT;
  ctx->ldt=0;
  ctx->ldt_limit=0;
  ctx->per_task_data=0;
}

void kernel_thread_helper(void (*fn)(void*), void *data)
{
  fn(data);
  sys_exit(0);
}

void initialize_idle_tasks(void)
{
  task_t *task;
  page_frame_t *ts_page;
  int r, cpu;
  cpu_sched_stat_t *sched_stat;

  for( cpu = 0; cpu < CONFIG_NRCPUS; cpu++ ) {
    ts_page = alloc_page(AF_ZERO);
    if( ts_page == NULL ) {
      panic( "initialize_idle_tasks(): Can't allocate main structure for idle task !" );  
    }

    task = pframe_to_virt(ts_page);
    idle_tasks[cpu] = task;

    /* Setup PIDs and default priorities. */
    spinlock_initialize(&task->lock);
    task->pid = task->ppid = 0;
    task->cpu = cpu;

    if( sched_setup_idle_task(task) != 0 ) {
      panic( "initialize_idle_task(): Can't setup scheduler details !" );
    }


    /* Initialize page tables to default kernel page directory. */
    ptable_ops.clone_rpd(&task->rpd, &kernel_rpd);

    /* Initialize kernel stack.
     * Since kernel stacks aren't properly initialized, we can't use standard
     * API that relies on 'cpu_id'.
     */
    if( allocate_kernel_stack(&task->kernel_stack) != 0 ) {
      panic( "initialize_idle_tasks(): Can't initialize kernel stack for idle task !" ); 
    }

    /* FIXME DK: redisign! */
    {
        page_frame_t *pf = alloc_pages(KERNEL_STACK_PAGES, 0);
        page_frame_iterator_t pfi;
        ITERATOR_CTX(page_frame, PF_ITER_INDEX) pfi_index_ctx;
        
        if (!pf)
            panic("Can't allocate %d pages for kernel stack!", KERNEL_STACK_PAGES);

        pfi_index_init(&pfi, &pfi_index_ctx, pframe_number(pf), pframe_number(pf) + KERNEL_STACK_PAGES - 1);
        iter_first(&pfi);
        r = ptable_ops.mmap(&task->rpd, task->kernel_stack.low_address, KERNEL_STACK_PAGES,
                            &pfi, PDE_RW | PDE_NX, true);
        if( r != 0 ) {
            panic("Can't map kernel stack for idle task !");
        }
    }
    /* Setup arch-specific task context. */
    __arch_setup_ctx(task,0,TPL_KERNEL);
  }

  /* Now initialize per-CPU scheduler statistics. */
  cpu = 0;
  for_each_percpu_var(sched_stat,cpu_sched_stat) {
    sched_stat->cpu = cpu;
    sched_stat->current_task = idle_tasks[cpu];
    sched_stat->kstack_top = idle_tasks[cpu]->kernel_stack.high_address;
    sched_stat->kernel_ds = KERNEL_SELECTOR(KDATA_DES);
    sched_stat->user_ds = USER_SELECTOR(UDATA_DES);
    cpu++;
  }
}

#ifdef CONFIG_TEST
bool kthread_cpu_autodeploy=false;
#endif

int kernel_thread(void (*fn)(void *), void *data, task_t **out_task)
{
  task_t *newtask;
  int r;

  r = create_task(current_task(),KERNEL_THREAD_FLAGS,TPL_KERNEL,&newtask,
                  NULL);

  if(r >= 0) {
    /* Prepare entrypoint for this kernel thread.
     * Currently this thread is set up to be executed from 'kernel_thread_helper()'.
     * But we should setup 'fn' and 'data' as parameters for 'kernel_thread_helper()'. */
     regs_t *regs = (regs_t *)(newtask->kernel_stack.high_address - sizeof(regs_t));

     regs->gpr_regs.rdi = (uint64_t)fn;
     regs->gpr_regs.rsi = (uint64_t)data;

#ifdef CONFIG_TEST
     if( kthread_cpu_autodeploy ) {
       int cpu=newtask->pid % CONFIG_NRCPUS;

       if( cpu != cpu_id() && sched_move_task_to_cpu(newtask,cpu) ) {
         kprintf("[!!!] kernel_thread(): Can't move kernel thread %d to CPU %d !\n",
                 newtask->pid,cpu);
       }
     }
#endif
     /* Start this task. */
     sched_change_task_state(newtask,TASK_STATE_RUNNABLE);
  }

  if( out_task ) {
    if( r >= 0 ) {
      *out_task=newtask;
    } else {
      *out_task=NULL;
    }
  }

  return r;
}

static uint64_t __setup_kernel_task_context(task_t *task)
{
  regs_t *regs = (regs_t *)(task->kernel_stack.high_address - sizeof(regs_t));

  /* Prepare a fake CPU-saved context */
  memset( regs, 0, sizeof(regs_t) );

  /* Now setup selectors so them reflect kernel space. */
  regs->int_frame.cs = KERNEL_SELECTOR(KTEXT_DES);
  regs->int_frame.old_ss = KERNEL_SELECTOR(KDATA_DES);
  regs->int_frame.rip = (uint64_t)kernel_thread_helper;

  /* When kernel threads start execution, their 'userspace' stacks are equal
   * to their kernel stacks.
   */
  regs->int_frame.old_rsp = task->kernel_stack.high_address - 128;
  regs->int_frame.rflags = KERNEL_RFLAGS;

  return sizeof(regs_t);
}

static uint64_t __setup_user_task_context(task_t *task)
{
  regs_t *regs = (regs_t *)(task->kernel_stack.high_address - sizeof(regs_t));
  /* Prepare a fake CPU-saved context */
  memset( regs, 0, sizeof(regs_t) );

  /* Now setup selectors so them reflect user space. */
  regs->int_frame.cs = USER_SELECTOR(UTEXT_DES);
  regs->int_frame.old_ss = USER_SELECTOR(UDATA_DES);
  regs->int_frame.rip = 0;
  regs->int_frame.old_rsp = 0;
  regs->int_frame.rflags = USER_RFLAGS;

  return sizeof(regs_t);
}

/* NOTE: This function doesn't reload LDT ! */
static void __setup_user_ldt( uintptr_t ldt )
{
  descriptor_t *ldt_root=(descriptor_t*)ldt;
  descriptor_t *ldt_dsc;

  if( !ldt ) {
    return;
  }

  /* Zeroize the NIL descriptor. */
  ldt_dsc=ldt_root;
  memset(ldt_dsc,0,sizeof(*ldt_dsc));

  /* Setup default PTD descriptor. */
  ldt_dsc=&ldt_root[PTD_SELECTOR];
  descriptor_set_base(ldt_dsc,0);
  ldt_dsc->access=AR_PRESENT | AR_DATA | AR_WRITEABLE | (1 << 2) | DPL_USPACE;
}

static void __apply_task_exec_attrs(regs_t *regs,exec_attrs_t *exec_attrs,
                                    arch_context_t *task_ctx)
{
  if( exec_attrs->stack ) {
    regs->int_frame.old_rsp=exec_attrs->stack;
  }

  regs->int_frame.rip=exec_attrs->entrypoint;
  regs->gpr_regs.rdi=exec_attrs->arg1;
  regs->gpr_regs.rsi=exec_attrs->arg2;

  task_ctx->per_task_data=exec_attrs->per_task_data;
}

int arch_setup_task_context(task_t *newtask,task_creation_flags_t cflags,
                            task_privelege_t priv,task_t *parent,
                            task_creation_attrs_t *attrs)
{
  uintptr_t fsave = newtask->kernel_stack.high_address;
  uint64_t delta, reg_size;
  arch_context_t *parent_ctx = (arch_context_t*)&parent->arch_context[0];
  arch_context_t *task_ctx;
  tss_t *tss;
  regs_t *regs;

  if( priv == TPL_KERNEL ) {
    reg_size = __setup_kernel_task_context(newtask);
  } else {
    reg_size = __setup_user_task_context(newtask);
  }

  /* Now reserve space for storing XMM context since it requires
   * the address to be 512-bytes aligned.
   */
  fsave-=reg_size;
  regs=(regs_t*)fsave;

  delta=fsave;
  fsave-=512;
  fsave &= 0xfffffffffffffff0;

  memset( (char *)fsave, 0, 512 );
  /* Save size of this context for further use in RESTORE_ALL. */
  fsave -= 8;
  *((uint64_t *)fsave) = delta;

  fsave -= 8;
  /* Now save the return point on the stack depending on type of the thread. */
  if( priv == TPL_KERNEL ) {
    *((uint64_t *)fsave) = (uint64_t)kthread_fork_path;
  } else {
    if( cflags & CLONE_COW ) {
      *((uint64_t *)fsave) = (uint64_t)user_fork_path_test;
    } else {
      *((uint64_t *)fsave) = (uint64_t)user_fork_path;
    }
  }

  /* Now setup CR3 and _current_ value of new thread's stack. */
  __arch_setup_ctx(newtask,(uint64_t)fsave,priv);

  task_ctx=(arch_context_t*)&newtask->arch_context[0];
  tss=parent_ctx->tss;
  if( tss ) {
    task_ctx->tss=tss;
    task_ctx->tss_limit=parent_ctx->tss_limit;
  }

  if( priv == TPL_USER ) {
      /* Allocate LDT for this task. */
    task_ctx->ldt_limit=LDT_ITEMS*sizeof(descriptor_t);
    task_ctx->ldt=(uintptr_t)memalloc(task_ctx->ldt_limit);

    if( !task_ctx->ldt ) {
      return -ENOMEM;
    }
    __setup_user_ldt(task_ctx->ldt);
  }

  if( attrs ) {
    __apply_task_exec_attrs(regs,&attrs->exec_attrs,task_ctx);
  }

  /* In case of COW, i.e. fork(), child continues execution from the same point
   * as its parent.
   */
  if( cflags & CLONE_COW ) {
    regs_t *pregs=(regs_t *)(parent->kernel_stack.high_address - sizeof(regs_t));

    regs->int_frame.rip=pregs->int_frame.rip;
    regs->int_frame.old_rsp=pregs->int_frame.old_rsp;
  }
  return 0;
}

int arch_process_context_control(task_t *task, ulong_t cmd,ulong_t arg)
{
  regs_t *regs=(regs_t *)(task->kernel_stack.high_address - sizeof(regs_t));
  int r=0;
  arch_context_t *arch_ctx;
  exec_attrs_t *attrs;
  ulong_t l;

  switch( cmd ) {
    case SYS_PR_CTL_SET_ENTRYPOINT:
      regs->int_frame.rip = arg;
      break;
    case SYS_PR_CTL_SET_STACK:
      regs->int_frame.old_rsp = arg;
      break;
    case SYS_PR_CTL_GET_ENTRYPOINT:
      r = regs->int_frame.rip;
      break;
    case SYS_PR_CTL_GET_STACK:
      r = regs->int_frame.old_rsp;
      break;
    case SYS_PR_CTL_SET_PERTASK_DATA:
      arch_ctx=(arch_context_t*)&task->arch_context[0];
      if( arch_ctx->ldt ) {
        descriptor_t *ldt_dsc=(descriptor_t*)arch_ctx->ldt;
        ldt_dsc=&ldt_dsc[PTD_SELECTOR];

        arch_ctx->per_task_data=arg;
        descriptor_set_base(ldt_dsc,arg);
        ldt_dsc->access=AR_PRESENT | AR_DATA | AR_WRITEABLE | (1 << 2) | DPL_USPACE;

        interrupts_disable();
        if( task == current_task() ) {
          load_ldt(cpu_id(),arch_ctx->ldt,arch_ctx->ldt_limit);
        }
        interrupts_enable();
      } else {
        r=-EINVAL;
      }
      break;
    case SYS_PR_CTL_REINCARNATE_TASK:
      attrs=(exec_attrs_t *)arg;
      arch_ctx=(arch_context_t*)&task->arch_context[0];

      /* Reset hardware context. */
      __setup_arch_segment_regs(arch_ctx,TPL_USER);
      __setup_user_task_context(task);
      __apply_task_exec_attrs(regs,attrs,arch_ctx);

      /* Setup XMM context. */
      l=((ulong_t)regs-512) & 0xfffffffffffffff0;
      memset( (char *)l, 0, 512 );

      r=arch_process_context_control(task,SYS_PR_CTL_SET_PERTASK_DATA,
                                     attrs->per_task_data);
      break;
    default:
      r=-EINVAL;
      break;
  }
  return r;
}
