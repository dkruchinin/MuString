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
 *
 * include/eza/smp.h: main kernel SMP-related macros,types and prototypes
 *
 */

#ifndef __SMP_H__
#define __SMP_H__

#include <eza/arch/types.h>

extern cpu_id_t online_cpus;

#ifdef CONFIG_SMP

  #ifndef NR_CPUS
    #define NR_CPUS 2
  #endif

  /* TODO: Handle it in a more beatiful way. */
  #define cpu_id() 0 /* arch_cpu_id() */

  #define define_per_cpu_var(var,type,cpu) \
    __attribute__((__section__(".data"))) type var##cpu

  #define DEFINE_PER_CPU(var,type) \
    define_per_cpu_var(var##_cpu_,type,0); \
    define_per_cpu_var(var##_cpu_,type,1)

  #define cpu_var(var,type) (type *)((char *)(&var##_cpu_0) + sizeof(type)*cpu_id());

  #define for_each_cpu_var(ptr,var,type) \
    for( ptr = (type *)(&var##_cpu_0); \
         ptr < (type *)((char *)&var##_cpu_0 + sizeof(*ptr)*NR_CPUS); ptr++ )

  #define EXTERN_PER_CPU(var,type) \
         extern type var##_cpu_0

#else /* CONFIG_SMP */
  #undef NR_CPUS
  #define NR_CPUS 1

  #define DEFINE_PER_CPU(var,type) type var##_cpu_0
  #define cpu_var(var,type) &var##_cpu_0

#endif

  static inline void set_cpu_online(cpu_id_t cpu, uint32_t online)
  {
    cpu_id_t mask = 1 << cpu;

    if( online ) {
      online_cpus |= mask;
    } else {
      online_cpus &= ~mask;
    }
  }

  #define for_each_cpu(c) \
    for(c = 0; c < NR_CPUS; c++ ) \

#endif /* __SMP_H__ */

