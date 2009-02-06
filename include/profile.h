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
 * (c) Copyright 2005,2008 Tirra <tirra.newly@gmail.com>
 *
 * include/profile.h: microkernel profiling
 *
 */

#ifndef __PROFILE_H__
#define __PROFILE_H__

#include <eza/arch/types.h>
#include <eza/arch/page.h>

/* definions */
#define STACK_SIZE           PAGE_SIZE
#define PROFILE_MEMORY_SIZE  ((2*4096)*1024)  /* 8M initially and overall */
#define PROFILE_INIT_TASK    32

/* Followed structure used for lining
 * allocations during the boot process
 */
typedef struct __boot_alloc {
  uintptr_t b;
  size_t size;
} boot_alloc_t;

/* Microkernel profile structure 
 * generally used for smp (in future) and 
 * varios memory mappings
 * FIXME: this structure will be extended due to the higher implementation
 */
typedef struct __profile_type {
  count_t cpus; /* number of CPUs */
  volatile count_t acpus; /* number of CPU that up and initied */
  uintptr_t base; /* base mui address */
  size_t mui_size; /* size of microkernel */
  uintptr_t stack_base; /* stack base address*/
  size_t stack_size; /* stack size */
} profile_t;

/* external (main.c) */
extern profile_t profile;
extern boot_alloc_t boot_allocs;

#endif /* __PROFILE_H__ */

