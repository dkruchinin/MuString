#ifndef __AMD64_CURRENT_H__
#define __AMD64_CURRENT_H__ 

#ifndef __ASM__

#include <eza/arch/types.h>
#include <eza/container.h>

struct __task_struct;

typedef struct __cpu_sched_stat {
  cpu_id_t cpu;
  struct __task_struct *current_task;
  uintptr_t kstack_top;
} cpu_sched_stat_t;

#endif

/* For low-level field accesses. */
#define CPU_SCHED_STAT_CPU_OFFT 0
#define CPU_SCHED_STAT_CURRENT_OFFT 0x8
#define CPU_SCHED_STAT_KSTACK_OFFT 0x10

#define read_css_field(field,v) \
  __asm__ volatile(  "movq %%gs:(%%rbx), %%rax" \
                     :"=r"(v) :"b" ( offset_of(cpu_sched_stat_t,field)) );

#define write_css_field(field,v) \
  __asm__ volatile(  "movq %%rax, %%gs:(%%rbx)" \
                     ::"a"(v), "b"( offset_of(cpu_sched_stat_t,field)) );

#endif

