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
 * eza/amd64/sched/task.c: AMD64-specific tasks-related functions.
 */

#include <ds/iterator.h>
#include <eza/task.h>
#include <eza/arch/context.h>
#include <mlibc/string.h>
#include <eza/arch/page.h>
#include <mlibc/kprintf.h>
#include <mm/mm.h>
#include <mm/page.h>
#include <mm/mmap.h>
#include <eza/errno.h>
#include <mlibc/string.h>
#include <eza/smp.h>
#include <mm/pfalloc.h>
#include <eza/kernel.h>
#include <eza/scheduler.h>
#include <eza/arch/scheduler.h>
#include <eza/arch/current.h>
#include <eza/process.h>
#include <eza/arch/profile.h>

/* Located on 'amd64/asm.S' */
extern void kthread_fork_path(void);
extern void user_fork_path(void);

/* Bytes enough to store our arch-specific context. */
#define ARCH_CONTEXT_BUF_SIZE  1256

/* Default kernel threads flags. */
#define KERNEL_THREAD_FLAGS  (CLONE_MM)

/* Per-CPU glabal structure that reflects the most important kernel states. */
cpu_sched_stat_t PER_CPU_VAR(cpu_sched_stat);

static void __arch_setup_ctx(task_t *newtask,uint64_t rsp)
{
  arch_context_t *ctx = (arch_context_t*)&(newtask->arch_context[0]);

  /* Setup CR3 */
  ctx->cr3 = _k2p((uintptr_t)pframe_to_virt(newtask->page_dir));
  ctx->rsp = rsp;
  ctx->fs = USER_SELECTOR(UDATA_DES);
  ctx->es = USER_SELECTOR(UDATA_DES);
  ctx->gs = USER_SELECTOR(UDATA_DES);
  ctx->ds = USER_SELECTOR(UDATA_DES);
  ctx->user_rsp = 0;

  /* Default TSS value which means: use per-CPU TSS. */
  ctx->tss=NULL;
  ctx->tss_limit=TSS_DEFAULT_LIMIT;
}

extern long __t1;
long __t2;

void kernel_thread_helper(void (*fn)(void*), void *data)
{
//  __READ_TIMESTAMP_COUNTER(__t2);
//  kprintf( "******* CONTEXT SWITCH TIME: %d\n", __t2 - __t1 );
//  for(;;);

    fn(data);
  l2: goto l2;
}


/* For initial stack filling. */
static page_frame_t *next_frame;

static void __iter_stub(page_frame_iterator_t *pfi)
{
  panic("unimplemented!");
}

static void acc_next_frame(page_frame_iterator_t *pfi)
{
  ASSERT(pfi->type == PF_ITER_ALLOC);

  pfi->state = ITER_RUN;
  if(next_frame != NULL ) {
    pfi->pf_idx = pframe_number(next_frame);
  } else {
    page_frame_t *frame = alloc_page(AF_PGEN);
    if( frame == NULL ) {
      panic( "initialize_idle_tasks(): Can't allocate a page !" );
    }

    pfi->pf_idx = pframe_number(frame);
  }
}

static void init_pfiter_alloc(page_frame_iterator_t *pfi)
{
  pfi->first = __iter_stub;
  pfi->last = __iter_stub;
  pfi->next = acc_next_frame;
  pfi->prev = __iter_stub;
  iter_init(pfi, PF_ITER_ALLOC);
  iter_set_ctx(pfi, NULL);
}

void initialize_idle_tasks(void)
{
  task_t *task;
  page_frame_t *ts_page;
  int r, cpu;
  cpu_sched_stat_t *sched_stat;
  mmap_info_t minfo;

  memset(&minfo, 0, sizeof(minfo));  
  init_pfiter_alloc(&minfo.pfi);
  for( cpu = 0; cpu < CONFIG_NRCPUS; cpu++ ) {
    ts_page = alloc_page(AF_PGEN | AF_ZERO);
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
    task->page_dir = kernel_root_pagedir;

    /* Initialize kernel stack.
     * Since kernel stacks aren't properly initialized, we can't use standard
     * API that relies on 'cpu_id'.
     */
    if( allocate_kernel_stack(&task->kernel_stack) != 0 ) {
      panic( "initialize_idle_tasks(): Can't initialize kernel stack for idle task !" ); 
    }

    next_frame = NULL;
    minfo.va_from = task->kernel_stack.low_address;
    minfo.va_to = minfo.va_from + ((KERNEL_STACK_PAGES - 1) << PAGE_WIDTH);
    minfo.flags = MAP_RW;
    r = mmap_pages(task->page_dir, &minfo);
    
    if( r != 0 ) {
      panic( "initialize_idle_tasks(): Can't map kernel stack for idle task !" );
    }
    /* Setup arch-specific task context. */
    __arch_setup_ctx(task,0);
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

status_t kernel_thread(void (*fn)(void *), void *data, task_t **out_task)
{
  task_t *newtask;
  status_t r;

  r = create_task(current_task(),KERNEL_THREAD_FLAGS,TPL_KERNEL,&newtask);

  if(r >= 0) {
    /* Prepare entrypoint for this kernel thread.
     * Currently this thread is set up to be executed from 'kernel_thread_helper()'.
     * But we should setup 'fn' and 'data' as parameters for 'kernel_thread_helper()'. */
     regs_t *regs = (regs_t *)(newtask->kernel_stack.high_address - sizeof(regs_t));

     regs->rdi = (uint64_t)fn;
     regs->rsi = (uint64_t)data;

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
  regs->cs = KERNEL_SELECTOR(KTEXT_DES);
  regs->old_ss = KERNEL_SELECTOR(KDATA_DES);
  regs->rip = (uint64_t)kernel_thread_helper;

  /* When kernel threads start execution, their 'userspace' stacks are equal
   * to their kernel stacks.
   */
  regs->old_rsp = task->kernel_stack.high_address - 128;
  regs->rflags = KERNEL_RFLAGS;

  return sizeof(regs_t);
}

static uint64_t __setup_user_task_context(task_t *task)
{
  regs_t *regs = (regs_t *)(task->kernel_stack.high_address - sizeof(regs_t));
  /* Prepare a fake CPU-saved context */
  memset( regs, 0, sizeof(regs_t) );

  /* Now setup selectors so them reflect user space. */
  regs->cs = USER_SELECTOR(UTEXT_DES);
  regs->old_ss = USER_SELECTOR(UDATA_DES);
  regs->rip = 0;
  regs->old_rsp = 0;
  regs->rflags = USER_RFLAGS;

  return sizeof(regs_t);
}

status_t arch_setup_task_context(task_t *newtask,task_creation_flags_t cflags,
                                 task_privelege_t priv)
{
  uintptr_t fsave = newtask->kernel_stack.high_address;
  uint64_t t2, delta, reg_size;

  if( priv == TPL_KERNEL ) {
    reg_size = __setup_kernel_task_context(newtask);
  } else {
    reg_size = __setup_user_task_context(newtask);
  }

  /* Now reserve space for storing XMM context since it requires
   * the address to be 512-bytes aligned.
   */
  fsave -= reg_size;

  /* Calculate offset to the nearest 512-bytes boundary. */
  t2 = fsave & 0xfffffffffffffe00;
  delta = fsave - t2;

  /* After this we will be 100% able to store 512-bytes XMM context. */
  delta += 512;

  /* Now prepare XMM context. */
  fsave -= delta;

  memset( (char *)fsave, 0, 512 );
  /* Save size of this context for further use in RESTORE_ALL. */
  fsave -= 8;
  *((uint64_t *)fsave) = delta;

  fsave -= 8;
  /* Now save the return point on the stack depending on type of the thread. */
  if( priv == TPL_KERNEL ) {
    *((uint64_t *)fsave) = (uint64_t)kthread_fork_path;
  } else {
    *((uint64_t *)fsave) = (uint64_t)user_fork_path;
  }

  /* Now setup CR3 and _current_ value of new thread's stack. */
  __arch_setup_ctx(newtask,(uint64_t)fsave);

  return 0;
}

status_t arch_process_context_control(task_t *task, ulong_t cmd,ulong_t arg)
{
  regs_t *regs = (regs_t *)(task->kernel_stack.high_address - sizeof(regs_t));
  status_t r = 0;
  
  switch( cmd ) {
    case SYS_PR_CTL_SET_ENTRYPOINT:
      regs->rip = arg;
      break;
    case SYS_PR_CTL_SET_STACK:
      regs->old_rsp = arg;
      break;
    case SYS_PR_CTL_GET_ENTRYPOINT:
      r = regs->rip;
      break;
    case SYS_PR_CTL_GET_STACK:
      r = regs->old_rsp;
      break;
  }
  return r;
}
