#ifndef __AMD64_CURRENT_H__
#define __AMD64_CURRENT_H__ 

#ifndef __ASM__

#include <eza/arch/types.h>
#include <eza/container.h>

struct __task_struct;

typedef uint64_t curtype_t;

typedef struct __cpu_sched_stat {
  curtype_t cpu;
  struct __task_struct *current_task;
  uintptr_t kstack_top;
  curtype_t flags, irq_count, preempt_count;
  curtype_t irq_lock_count;
} cpu_sched_stat_t;

#endif

/* For low-level field accesses. */
#define CPU_SCHED_STAT_CPU_OFFT 0
#define CPU_SCHED_STAT_CURRENT_OFFT 0x8
#define CPU_SCHED_STAT_KSTACK_OFFT 0x10
#define CPU_SCHED_STAT_FLAGS_OFFT 0x18
#define CPU_SCHED_STAT_IRQCNT_OFFT 0x20
#define CPU_SCHED_STAT_PREEMPT_OFFT 0x28
#define CPU_SCHED_STAT_IRQLOCK_OFFT 0x30

/* Task flags indexes  */
#define CPU_SCHED_NEED_RESCHED_F_IDX 0

/* Task flags masks. */
#define CPU_SCHED_NEED_RESCHED_F_MASK (1 << CPU_SCHED_NEED_RESCHED_F_IDX)

#ifndef __ASM__

#define read_css_field(field,v) \
  __asm__ __volatile__(  "movq %%gs:(%%rax), %%rax" \
			 :"=r"(v) : "a" ( offset_of(cpu_sched_stat_t,field)) );

#define write_css_field(field,v) \
  __asm__ volatile(  "movq %%rax, %%gs:(%%rbx)" \
                     ::"a"(v), "b"( offset_of(cpu_sched_stat_t,field)) );

#define inc_css_field(field) \
  __asm__ volatile(  "incq %%gs:(%%rax)" \
                     :: "a"(offset_of(cpu_sched_stat_t,field)) );


#define dec_css_field(field) \
  __asm__ volatile(  "decq %%gs:(%%rax)" \
                     :: "a"(offset_of(cpu_sched_stat_t,field)) );

#define set_css_task_flag(flag) \
  __asm__ volatile(  "bts %%rax, %%gs:(%%rbx)" \
                     :: "a"(flag), "b"(offset_of(cpu_sched_stat_t,flags)) );

#define reset_css_task_flag(flag) \
  __asm__ volatile(  "btr %%rax, %%gs:(%%rbx)" \
                     :: "a"(flag), "b"(offset_of(cpu_sched_stat_t,flags)) );


struct __task_struct;


static inline void arch_sched_set_current_need_resched(void)
{
  set_css_task_flag(CPU_SCHED_NEED_RESCHED_F_IDX);
}

static inline void arch_sched_reset_current_need_resched(void)
{
  reset_css_task_flag(CPU_SCHED_NEED_RESCHED_F_IDX);
}

static inline bool current_task_needs_resched(void)
{
  uintptr_t ct;

  read_css_field(flags,ct);
  return (ct & CPU_SCHED_NEED_RESCHED_F_MASK) ? 1 : 0;
}

static inline struct __task_struct *current_task(void)
{
  uintptr_t ct;

  read_css_field(current_task,ct);
  return (struct __task_struct*)ct;
}

#endif

#endif

