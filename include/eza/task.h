
#ifndef __TASK_H__
#define __TASK_H__ 

#include <eza/arch/types.h>
#include <eza/scheduler.h>
#include <eza/kstack.h>
#include <eza/arch/context.h>

typedef enum __task_creation_flag_t {
  CLONE_MM = 0x1,
} task_creation_flags_t;

int setup_task_kernel_stack(task_t *task);
int initialize_stack_system_area(kernel_task_data_t *task);
void initialize_task_system_data(kernel_task_data_t *task, cpu_id_t cpu);

status_t kernel_thread(void (*fn)(void *), void *data);
status_t arch_copy_process(task_t *parent,task_t *newtask,void *arch_ctx,
                           task_creation_flags_t flags);

status_t do_fork(void *arch_ctx, task_creation_flags_t flags);
status_t create_new_task(task_t *parent, task_t **t, task_creation_flags_t flags);
void free_task_struct(task_t *task);

#endif

