
#ifndef __ARCH_PREEMPT_H__
#define __ARCH_PREEMPT_H__

#include <eza/arch/current.h>
#include <eza/arch/types.h>

extern void schedule(void);
extern cpu_id_t online_cpus;

static inline void preempt_disable(void)
{
  if( online_cpus != 0 ) {
    inc_css_field(preempt_count);
  }
}

static bool in_atomic(void)
{
  uint64_t c;

  read_css_field(preempt_count,c);
  return (c > 0);
}

static inline void preempt_enable(void)
{
  if( online_cpus != 0 ) {
    dec_css_field(preempt_count);
    if( !in_atomic() && current_task_needs_resched() ) {
      schedule();
    }
  }
}


static int read_atomic_counter(void)
{
  uint64_t c;

  read_css_field(preempt_count,c);
  return c;
}

#endif
