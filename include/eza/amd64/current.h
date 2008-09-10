#ifndef __AMD64_CURRENT_H__
#define __AMD64_CURRENT_H__ 

#include <eza/arch/types.h>
#include <eza/container.h>

struct __task_struct;

typedef struct __cpu_sched_stat {
  cpu_id_t cpu;
  struct __task_struct *current_task;
  uintptr_t kstack_top;
} cpu_sched_stat_t;

#define read_css_field(field,v) \
  __asm__ volatile(  "movq %%gs:8, %%rax" \
                     :"=r"(v) :"b" ( offset_of(cpu_sched_stat_t,current_task)) );

#endif

