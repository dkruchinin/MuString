#include <eza/kernel.h>
#include <mlibc/kprintf.h>
#include <eza/smp.h>
#include <eza/arch/scheduler.h>
#include <eza/arch/types.h>
#include <eza/arch/bits.h>
#include <eza/task.h>
#include <mm/pt.h>
#include <eza/scheduler.h>
#include <eza/swks.h>
#include <eza/kstack.h>
#include <mm/pagealloc.h>
#include <eza/arch/page.h>
#include <eza/pageaccs.h>

kernel_task_data_t * idle_tasks[NR_CPUS];

static void clone_fn(void *data)
{
  
}

void idle_loop(void)
{
  int target_tick = swks.system_ticks_64 + 100;
  task_t *new;
  int r;

  kprintf( "=====================================\n" );
  kernel_thread(NULL,NULL);
  //r = create_new_task( &new, CLONE_MM );
  //if( r == 0 ) {
  //  kprintf( " ** New task: PID: %d\n", new->pid );
  //} else {
  //  kprintf( "[!!!] Task creation failed: r = %d\n", r  );
  // }
  kprintf( "=====================================\n" );

  /* TODO: Enable rescheduling interrupts here. */
  for( ;; ) {
    if( swks.system_ticks_64 == target_tick ) {
      kprintf( " - Tick, tick ! (ticks: %d, My PID: %d, My priorirty: %d, CPU ID: %d\n",
               swks.system_ticks_64, current_task()->pid, current_task()->priority,
               system_sched_data()->cpu_id );
      target_tick += 10150;
    }
  }
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

  for( cpu = 0; cpu < NR_CPUS; cpu++ ) {
    ts_page = alloc_page(0,1);
    if( ts_page == NULL ) {
      panic( "initialize_idle_tasks(): Can't allocate main structure for idle task !" );  
    }

    idle_tasks[cpu] = (kernel_task_data_t *)pframe_to_virt(ts_page);
    task = &(idle_tasks[cpu]->task);

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

    /* OK, stack is already mapped, so we can map task struct into this task's address
     * space.
     */
    next_frame = ts_page;
    r = mm_map_pages( &task->page_dir, &idle_pacc,
                      task->kernel_stack.low_address & KERNEL_STACK_MASK, 1,
                      KERNEL_STACK_PAGE_FLAGS, NULL );
    if( r != 0 ) {
      panic( "initialize_idle_tasks(): Can't map kernel stack for idle task !" );
    }

    kprintf( ">>>> Stack: high address: 0x%X, Low address: 0x%X <<<<\n",
             task->kernel_stack.high_address, task->kernel_stack.low_address );

    /* OK, now kernel stack is ready for this idle task. Finally, initialize its
     * 'system_data' structure.
     */
    initialize_task_system_data(idle_tasks[cpu], cpu);
  }
}

