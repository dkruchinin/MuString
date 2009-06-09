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
 * include/arch/preempt.h
 *
 */

#ifndef __ARCH_PREEMPT_H__
#define __ARCH_PREEMPT_H__

#include <arch/types.h>
#include <arch/current.h>
#include <arch/interrupt.h>

extern void schedule(void);
extern volatile cpu_id_t online_cpus;

#define COND_RESCHED_CURRENT \
    if( !in_atomic() && current_task_needs_resched() ) { \
        schedule();                                      \
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
