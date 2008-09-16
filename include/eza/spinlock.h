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
 * (c) Copyright 2008 Tirra <tirra.newly@gmail.com>
 *
 * include/eza/spinlock.h: spinlock and atomic types and functions
 *                         architecture independed
 *
 */

#ifndef __EZA_SPINLOCK_H__ /* there are several spinlock.h with different targets */
#define __EZA_SPINLOCK_H__

#include <eza/arch/types.h>
#include <eza/arch/mbarrier.h>
#include <eza/arch/preempt.h>

typedef struct __atomic {
  volatile long c;
} atomic_t;

/* atomic related functions prototypes
 * used in implementation
 */
static inline void atomic_set(atomic_t *v,long c);
static inline long atomic_get(atomic_t *v);

#ifdef CONFIG_SMP
typedef struct __spinlock_type {
  atomic_t v;
} spinlock_t;


#define spinlock_initialize(x,y)
#define spinlock_trylock(x)
#define mbarrier_leave()
//#define arch_atomic_spinlock(x)

#define spinlock_declare(s)  spinlock_t s
#define spinlock_extern(s)   extern spinlock_t s

#define spinlock_initialize_macro(s)  \
  spinlock_t s={ \
    .v={0};	 \
  };

#define spinlock_lock(u) \
  preempt_disable(); \
  //  arch_atomic_spinlock(&(u->v))

static inline void spinlock_unlock(spinlock_t *s)
{
  mbarrier_leave();
  //  atomic_set((&s->v),0);

  /* Enable preemption. */
  preempt_enable();
}

//extern void spinlock_initialize(spinlock_t *s,const char *name);
//extern int spinlock_trylock(spinlock_t *s);


#else /* just disable preemption while spin is locked */
/* on UP systems you can just disable preemtion and/or interrupts to make a spinlock */
/*FIXME: include all preemtion enable/disable stuff */

typedef long spinlock_t;

#define spinlock_declare(s)  
#define spinlock_extern(s)   
#define spinlock_initialize_macro(s)  

#define spinlock_initialize(x, name)
#define spinlock_lock(x) /* disable preemtion */
#define spinlock_trylock(x) /* the same like above */
#define spinlock_unlock(x) /* enable preemtion */

#endif /* CONFIG_SMP */

static inline void atomic_set(atomic_t *v,long c)
{
  v->c=c;

  return;
}

static inline long atomic_get(atomic_t *v)
{
  return v->c;
}

#endif /* __EZA_SPINLOCK_H__ */

