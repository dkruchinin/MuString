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
 *
 * include/arch/current.h
 *
 */

#ifndef __AMD64_CURRENT_H__
#define __AMD64_CURRENT_H__ 

#ifndef __ASM__

#include <mstring/stddef.h>
#include <mstring/types.h>
#include <arch/cpu.h>

struct __task_struct;

typedef ulong_t curtype_t;

typedef struct __cpu_sched_stat {
  curtype_t cpu;
  struct __task_struct *current_task;
  uintptr_t kstack_top;
  curtype_t flags, irq_count, preempt_count;
  curtype_t irq_lock_count, kernel_ds, user_ds;
  curtype_t user_stack;
  curtype_t user_es,user_fs,user_gs;
  curtype_t uspace_works;
  curtype_t per_task_data;
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
#define CPU_SCHED_STAT_KERN_DS_OFFT 0x38
#define CPU_SCHED_STAT_USER_DS_OFFT 0x40
#define CPU_SCHED_STAT_USTACK_OFFT 0x48
#define CPU_SCHED_STAT_USER_ES_OFFT 0x50
#define CPU_SCHED_STAT_USER_FS_OFFT 0x58
#define CPU_SCHED_STAT_USER_GS_OFFT 0x60
#define CPU_SCHED_STAT_USER_WORKS_OFFT 0x68  /* It's a pointer ! */
#define CPU_SCHED_STAT_USER_PTD_OFFT  0x70

/* Task flags indexes  */
#define CPU_SCHED_NEED_RESCHED_F_IDX          0
#define CPU_SCHED_DEF_WORKS_F_IDX             1
#define CPU_SCHED_DEF_WORKS_PROCESSING_F_IDX  2

/* Task flags masks. */
#define CPU_SCHED_NEED_RESCHED_F_MASK (1 << CPU_SCHED_NEED_RESCHED_F_IDX)
#define CPU_SCHED_DEF_WORKS_F_MASK    (1 << CPU_SCHED_DEF_WORKS_F_IDX)

#ifndef __ASM__

#define read_css_field(field,v) \
  __asm__ __volatile__(  "movq %%gs:(%1), %%rax" \
                         :"=a"(v) : "R" ( offsetof(cpu_sched_stat_t,field)) );

#define write_css_field(field,v) \
  __asm__ volatile(  "movq %%r, %%gs:(%%rbx)" \
                     ::"a"(v), "b"( offsetof(cpu_sched_stat_t,field)) : "%rax" );

#define inc_css_field(field) \
  __asm__ volatile(  "incq %%gs:(%0)" \
                     :: "r"(offsetof(cpu_sched_stat_t,field)) );

#define dec_css_field(field) \
  __asm__ volatile(  "decq %%gs:(%0)" \
                     :: "r"(offsetof(cpu_sched_stat_t,field)) );

#define set_css_task_flag(flag) \
  __asm__ volatile(  "bts %0, %%gs:(%1)" \
                     :: "r"(flag), "r"(offsetof(cpu_sched_stat_t,flags)) );

#define reset_css_task_flag(flag) \
  __asm__ volatile(  "btr %0, %%gs:(%1)" \
                     :: "r"(flag), "r"(offsetof(cpu_sched_stat_t,flags)) );

struct __task_struct;

static inline void arch_sched_set_current_need_resched(void)
{
  set_css_task_flag(CPU_SCHED_NEED_RESCHED_F_IDX);
}

static inline void arch_sched_reset_current_need_resched(void)
{
  reset_css_task_flag(CPU_SCHED_NEED_RESCHED_F_IDX);
}

static inline void arch_sched_set_def_works_pending(void)
{
  set_css_task_flag(CPU_SCHED_DEF_WORKS_F_IDX);
}

static inline void arch_sched_reset_def_works_pending(void)
{
  reset_css_task_flag(CPU_SCHED_DEF_WORKS_F_IDX);
}

extern cpu_sched_stat_t __percpu_var_cpu_sched_stat[];

static inline void arch_sched_set_cpu_need_resched(cpu_id_t cpu)
{
  __percpu_var_cpu_sched_stat[cpu].flags |= (1<< (CPU_SCHED_NEED_RESCHED_F_IDX));
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

#define set_userspace_stack_pointer(s)          \
  __asm__ __volatile__( "movq %0, %%gs:(%1)"    \
                        :: "R"((uintptr_t)(s)), \
                         "R"((uintptr_t)CPU_SCHED_STAT_USTACK_OFFT) );

static inline uintptr_t get_userspace_stack_pointer(void)
{
  uintptr_t s;

  __asm__ __volatile__( "movq %%gs:(%1), %0"
                        :"=r"(s)
                        : "R"((uintptr_t)CPU_SCHED_STAT_USTACK_OFFT),
                        "R"((uintptr_t)0) );
  return s;
}


#endif   /* !__ASM__ */

#endif

