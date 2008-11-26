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
 * (c) Copyright 2008 Michael Tsymbalyuk <mtzaurus@gmail.com>
 * (c) Copytight 2008 Dan Kruchinin <dan.kruchinin@gmai.com>
 *
 * include/eza/smp.h: main kernel SMP-related macros,types and prototypes
 *
 */

#ifndef __SMP_H__
#define __SMP_H__

#include <config.h>
#include <eza/arch/cpu.h>
#include <eza/arch/types.h>
#include <eza/arch/scheduler.h>

extern volatile cpu_id_t online_cpus;

#ifdef CONFIG_SMP
/* Real cpu_id() is defined in include/<arch>/scheduler.h  */
#define __CPUS MAX_CPUS
#else
#define cpu_id() 0
#define __CPUS 1
#endif /* CONFIG_SMP */

#define PER_CPU_VAR(name)                       \
  __percpu_var_##name[MAX_CPUS] __percpu__

#define raw_percpu_get_var(name,cpu)                    \
  ({ __percpu_var_##name + cpu; })

#define percpu_get_var(name)                    \
  ({ __percpu_var_##name + cpu_id(); })

#define percpu_set_var(name, value)             \
  (__percpu_var_##name[cpu_id()] = (value))

#define for_each_percpu_var(ptr, name)          \
  for ((ptr) = __percpu_var_##name;             \
       (ptr) < (__percpu_var_##name + __CPUS);  \
       (ptr)++)

static void set_cpu_online(cpu_id_t cpu, uint32_t online);
static bool is_cpu_online(cpu_id_t cpu);


#define for_each_cpu(c)           \
  for(c = 0; c < NR_CPUS; c++ )   \

#define ONLINE_CPUS_MASK  (online_cpus & ((1<<NR_CPUS)-1) )

#endif /* __SMP_H__ */

