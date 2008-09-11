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

#include <eza/task.h>
#include <eza/arch/context.h>
#include <mlibc/string.h>
#include <eza/arch/page.h>
#include <mlibc/kprintf.h>
#include <mm/mm.h>
#include <eza/errno.h>
#include <mlibc/string.h>
#include <eza/smp.h>
#include <eza/pageaccs.h>
#include <mm/pagealloc.h>
#include <eza/kernel.h>
#include <eza/scheduler.h>
#include <eza/arch/scheduler.h>
#include <eza/arch/current.h>

/* Located on 'amd64/asm.S' */
extern void kthread_fork_path(void);

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
  ctx->cr3 = _k2p((uintptr_t)&(newtask->page_dir.entries[0]));
  ctx->rsp = rsp;
}

void kernel_thread_helper(void (*fn)(void*), void *data)
{
  fn(data);
  l2: goto l2;
}


/* For initial stack filling. */
static page_frame_t *next_frame;
static page_idx_t acc_next_frame(void *ctx)
{
  if(next_frame != NULL ) {
    return next_frame->idx;
  } else {
    page_frame_t *frame = alloc_page(0,0);
    if( frame == NULL ) {
      panic( "initialize_idle_tasks(): Can't allocate a page !" );
    }

    return frame->idx;
  }
}

static page_frame_accessor_t idle_pacc = {
  .frames_left = pageaccs_frames_left_stub,
  .next_frame = acc_next_frame,
  .reset = pageaccs_reset_stub,
  .alloc_page = pageaccs_alloc_page_stub,
};

void initialize_idle_tasks(void)
{
  task_t *task;
  page_frame_t *ts_page;
  int r, cpu;
  cpu_sched_stat_t *sched_stat;

  for( cpu = 0; cpu < MAX_CPUS; cpu++ ) {
    ts_page = alloc_page(0,1);
    if( ts_page == NULL ) {
      panic( "initialize_idle_tasks(): Can't allocate main structure for idle task !" );  
    }

    task = pframe_to_virt(ts_page);
    idle_tasks[cpu] = task;

    /* Setup PIDs and default priorities. */
    task->pid = task->ppid = 0;
    task->priority = task->static_priority = IDLE_TASK_PRIORITY;
    task->time_slice = 0;
    task->state = TASK_STATE_RUNNING;

    /* Initialize page tables to default kernel page directory. */
    initialize_page_directory(&task->page_dir);
    task->page_dir.entries = kernel_pt_directory.entries;

    /* Initialize kernel stack.
     * Since kernel stacks aren't properly initialized, we can't use standard
     * API that relies on 'cpu_id'.
     */
    if( allocate_kernel_stack(&task->kernel_stack) != 0 ) {
      panic( "initialize_idle_tasks(): Can't initialize kernel stack for idle task !" ); 
    }

    next_frame = NULL;
    r = mm_map_pages( &task->page_dir, &idle_pacc,
                      task->kernel_stack.low_address, KERNEL_STACK_PAGES,
                      KERNEL_STACK_PAGE_FLAGS, NULL );
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
    cpu++;
  }
}

status_t kernel_thread(void (*fn)(void *), void *data)
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
  }
  return r;
}

static uint64_t __setup_kernel_task_context(task_t *task)
{
  regs_t *regs = (regs_t *)(task->kernel_stack.high_address - sizeof(regs_t));
  uint64_t flags;

  /* Prepare a fake CPU-saved context */
  memset( regs, 0, sizeof(regs_t) );

  /* Now setup selectors so them reflect kernel space. */
  regs->cs = gdtselector(KTEXT_DES);
  regs->old_ss = gdtselector(KDATA_DES);
  regs->rip = (uint64_t)kernel_thread_helper;

  /* When kernel threads start execution, their 'userspace' stacks are equal
   * to their kernel stacks.
   */
  regs->old_rsp = task->kernel_stack.high_address - 128;

  /* Save flags. */
  __asm__ volatile (
    "pushfq\n"
    "popq %0\n"
    : "=r" (flags) );
  regs->rflags = flags | 0x200; /* Enable interrupts. */  

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
    panic( "arch_setup_task_context(): Creation of userspace threads is not yet supported !\n" );
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
    panic( "arch_setup_task_context(): Creation of userspace threads is not yet supported !\n" );
  }

  /* Now setup CR3 and _current_ value of new thread's stack. */
  __arch_setup_ctx(newtask,(uint64_t)fsave);

  return 0;
}

