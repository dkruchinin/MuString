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
 * (c) Copyright 2006,2007,2008 MString Core Team <http://mstring.jarios.org>
 * (c) Copyright 2008 Michael Tsymbalyuk <mtzaurus@gmail.com>
 * (c) Copytight 2008 Dan Kruchinin <dan.kruchinin@gmai.com>
 *
 * include/mstring/smp.h: main kernel SMP-related macros,types and prototypes
 *
 */

#ifndef __MSTRING_SMP_H__
#define __MSTRING_SMP_H__

#include <config.h>
#include <arch/cpu.h>
#include <arch/scheduler.h>
#include <mstring/kprintf.h>
#include <mstring/panic.h>
#include <mstring/types.h>

extern volatile cpu_id_t online_cpus;

#ifdef CONFIG_SMP
#include <ds/list.h>

#define __CPUS CONFIG_NRCPUS

typedef struct __smp_hook {
  int (*hook)(cpu_id_t cpuid, void *arg);
  list_node_t node;
  void *arg;
  char *name;
} smp_hook_t;

extern list_head_t smp_hooks;
static inline void smp_hook_register(smp_hook_t *hook)
{
  list_add2tail(&smp_hooks, &hook->node);
}

static inline void smp_hooks_fire(cpu_id_t cpuid)
{
  smp_hook_t *h;
  int ret, i = 0;

  list_for_each_entry(&smp_hooks, h, node) {
    kprintf_dbg("[CPU #%d]: Fired SMP hook: %s, %p\n", cpuid, h->name, h->hook);
    ret = h->hook(cpuid, h->arg);
    if (ret)
      panic("Failed to execute SMP hook #%d: ERR: %d!", i, ret);

    i++;
  }
}

#else
#define smp_hook_register(hook)
#define smp_hooks_fire(x)
#define cpu_id() 0
#define __CPUS 1
#endif /* CONFIG_SMP */

#define PER_CPU_VAR(name)                       \
  __percpu_var_##name[CONFIG_NRCPUS]

#define raw_percpu_get_var(name, cpu)           \
  ({ __percpu_var_##name + (cpu); })
#define raw_percpu_set_var(name, cpu, value)    \
  (__percpu_var_##name[(cpu)] = (value))
#define percpu_get_var(name) raw_percpu_get_var(name, cpu_id())
#define percpu_set_var(name, value) raw_percpu_set_var(name, cpu_id(), value)

#define for_each_percpu_var(ptr, name)          \
  for ((ptr) = __percpu_var_##name;             \
       (ptr) < (__percpu_var_##name + __CPUS);  \
       (ptr)++)

static inline void set_cpu_online(cpu_id_t cpu, uint32_t online)
{
  cpu_id_t mask = 1 << cpu;

  if( online ) {
    online_cpus |= mask;
  } else {
    online_cpus &= ~mask;
  }

  /* Tra-ta-ta-ta-ta! */
  smp_hooks_fire(cpu);
}

static inline bool is_cpu_online(cpu_id_t cpu)
{
  return !!(online_cpus & (1 << cpu));
}

#define for_each_cpu(c)           \
  for(c = 0; c < CONFIG_NRCPUS; c++ )   \

#define ONLINE_CPUS_MASK  (online_cpus & ((1<<CONFIG_NRCPUS)-1) )

#endif /* __MSTRING_SMP_H__ */

