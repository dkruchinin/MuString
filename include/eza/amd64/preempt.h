
#ifndef __ARCH_PREEMPT_H__
#define __ARCH_PREEMPT_H__

#include <eza/arch/types.h>
#include <eza/arch/current.h>
#include <eza/arch/interrupt.h>

extern void schedule(void);
extern volatile cpu_id_t online_cpus;

#define COND_RESCHED_CURRENT \
    if( !in_atomic() && current_task_needs_resched() ) { \
      schedule(); \
    }

static inline void preempt_disable(void)
{
  if( online_cpus != 0 ) {
    inc_css_field(preempt_count);
  }
}

static inline bool in_interrupt(void)
{
  uint64_t c;

  if( !online_cpus ) {
    return false;
  }

  read_css_field(irq_count,c);
  return c > 0;
}

static bool in_atomic(void)
{
  uint64_t c;

  if( !online_cpus ) {
    return false;
  }

  read_css_field(preempt_count,c);
  return (c > 0 || in_interrupt() || !is_interrupts_enabled() );
}

static inline void preempt_enable(void)
{
  if( online_cpus != 0 ) {
    dec_css_field(preempt_count);
    COND_RESCHED_CURRENT;
  }
}

static inline void cond_reschedule(void)
{
  COND_RESCHED_CURRENT;
}

static inline void lock_local_interrupts(void)
{
  interrupts_disable();
  if( online_cpus != 0 ) {
    inc_css_field(irq_lock_count);
  }
}

static inline void unlock_local_interrupts(void)
{
  if( online_cpus != 0 ) {
    curtype_t v;

    /* We don't have to read current irq lock count atomically
     * since we assume that interrupts we disabled and, therefore,
     * we're in atomic.
     */
    read_css_field(irq_lock_count,v);
    if( v > 0 ) {
      dec_css_field(irq_lock_count);
      if( v == 1 ) {
        /* The last lock was removed. */
        interrupts_enable();
        COND_RESCHED_CURRENT;
      }
    }
  } else {
    interrupts_enable();
    COND_RESCHED_CURRENT;
  }
}

static inline bool local_interrupts_locked(void)
{
  curtype_t v;
  
  read_css_field(irq_lock_count,v);
  return v > 0;
}


#endif
