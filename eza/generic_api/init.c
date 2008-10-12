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
 * eza/generic_api/init.c: contains implementation of the 'init' startup logic.
 *
 */

#include <eza/kernel.h>
#include <mlibc/kprintf.h>
#include <eza/smp.h>
#include <eza/arch/scheduler.h>
#include <eza/arch/types.h>
#include <eza/task.h>
#include <eza/scheduler.h>
#include <eza/swks.h>
#include <mlibc/string.h>
#include <eza/arch/mm_types.h>
#include <eza/arch/preempt.h>
#include <eza/process.h>
#include <eza/scheduler.h>
#include <ds/iterator.h>
#include <mm/mm.h>
#include <mm/pfalloc.h>
#include <mlibc/string.h>
#include <mm/pt.h>

#define INIT_CODE_START 0x1FFFFF000000
#define INIT_CODE_PAGES 1

#define INIT_STACK_START INIT_CODE_START+0x100000
#define INIT_STACK_PAGES 4

#define STEP 300

/* ovl 0x0,%rax; syscall; jmp <spin> */
// 0x0f, 0x05,
//                             0xe6,0x10,
static char init_code1[] = { 0x48, 0x31, 0xc0, \
                            0xe6, 0x0,       \
                            0x48, 0x89, 0xc3, \
                             0xeb, 0xf6 };

static char init_code[] = { 0x48, 0xc7,0xc0,0x08,0x00,0x00,0x00,        \
                            0x48, 0xc7, 0xc7, 0x10, 0x00, 0x00, 0x00,   \
                            0x48, 0xc7, 0xc6, 0x32, 0x00, 0x00, 0x00,   \
                            0x0f, 0x05,                                 \
                            0x48, 0x89, 0xc3,                           \
                            0x48, 0x09, 0xc0,                           \
                            0x75, 0x02,                                 \
                            0xe4, 0x32,                                 \
                            0x48, 0xc7,0xc0,0x09,0x00,0x00,0x00,        \
                            0x48, 0xc7, 0xc7, 0x12, 0x00, 0x00, 0x00,   \
                            0x48, 0xc7, 0xc6, 0x20, 0x00, 0x00, 0x00,   \
                            0x0f, 0x05,                                 \
                            0xe4, 0x11,                                 \
                            0x48, 0x89, 0xc3,                           \
                            0xeb, 0xfe };

#define INIT_CODE_SIZE  65

static void t(void)
{
    __asm__ __volatile__(
        "movq $8,%rax\n"
        "movq $0x10,%rdi\n"
        "movq $1,%rsi\n"
        "syscall\n"
        "movq %rax, %rbx\n"
        "orq  %rax,%rax\n"
        "jnz 2f\n"
        "inb $0x10\n"
        "2: jmp 2b\n");

}

static int create_init_mm(task_t *task)
{
  ITERATOR_CTX(page_frame, PF_ITER_INDEX) pfi_idx_ctx;
  page_frame_iterator_t pfi;
  page_frame_t *code = alloc_pages(INIT_CODE_PAGES, AF_PGEN);
  page_frame_t *stack = alloc_pages(INIT_STACK_PAGES, AF_PGEN);
  status_t r;
  page_idx_t idx;

  if( code == NULL ) {
    panic( "Can't allocate pages for init's code !" );
  }

  if( stack == NULL ) {
    panic( "Can't allocate pages for init's stack !" );
  }

  kprintf( "** INIT CODE START: %p, page idx: %x\n",
           INIT_CODE_START, code->idx );

  kprintf( "** INIT STACK START: %p, page idx: %x\n",
           INIT_STACK_START, stack->idx );

  mm_init_pfiter_index(&pfi, &pfi_idx_ctx,
                       pframe_number(code),
                       pframe_number(code) );

  r = mm_map_pages( &task->page_dir, &pfi,
                    INIT_CODE_START, INIT_CODE_PAGES,
                    MAP_ACC_MASK );
  if( r != 0 ) {
    goto out;
  }

  mm_init_pfiter_index(&pfi, &pfi_idx_ctx,
                       pframe_number(stack),
                       pframe_number(stack) + INIT_STACK_PAGES-1 );
  r = mm_map_pages( &task->page_dir, &pfi,
                    INIT_STACK_START, INIT_STACK_PAGES,
                    MAP_ACC_MASK );

  r = do_task_control(task,SYS_PR_CTL_SET_ENTRYPOINT,INIT_CODE_START);
  kprintf( "** Setting entrypoint: %d\n", r );

  r |= do_task_control(task,SYS_PR_CTL_SET_STACK,INIT_STACK_START +
                       PAGE_SIZE*INIT_STACK_PAGES - 32);
  kprintf( "** Setting stack: %d\n", r );

  idx = mm_pin_virtual_address(&task->page_dir,INIT_CODE_START);
  kprintf( "** Init code page: %x\n", idx );

  idx = mm_pin_virtual_address(&task->page_dir,KERNEL_BASE + 0x8000);
  kprintf( "** Kernel base: %p, first page: %d\n", KERNEL_BASE + 0x8000,
           idx );

  memcpy(pframe_to_virt(code),init_code,INIT_CODE_SIZE);
  
  if( r != 0 ) {
    goto out;
  }
out:
  return r;
}

void start_userspace_init(void)
{
  status_t r;
  task_t *init;

  r = create_task( current_task(), 0, TPL_USER, &init );
  kprintf( "**** Create inittask: %d\n", r );
  if( !r ) {
    r = create_init_mm(init);
  }

  if( !r ) {
    kprintf( "** Init is ready ! PID: %d\n", init->pid );
    r = sched_change_task_state( init, TASK_STATE_RUNNABLE );
  }

  if( r ) {
    panic("start_init(): Can't start 'init' process !");
  }
}

static void test_init_thread(void *data)
{
  int round = 0;
  uint64_t target_tick = swks.system_ticks_64 + 100;

  start_userspace_init();

  for(;;) {
    if( swks.system_ticks_64 >= target_tick ) {
      kprintf( " + [Init #%d] Tick, tick ! (Ticks: %d, PID: %d, ATOM: %d)\n",
               cpu_id(), swks.system_ticks_64, current_task()->pid, in_atomic() );
      target_tick += 300;
    }
  }
}

void start_init(void)
{
  if( kernel_thread(test_init_thread,NULL) != 0 ) {
    panic( "Can't start init thread !" );
  }
}
