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
 * (c) Copyright 2008 Tirra <tirra.newly@gmail.com>
 *
 * include/eza/amd64/mbarrier.h: amd64 specific memory barrier functions for prevent
 *                               critical code sections - used in spinlocks and preemptions
 *
 */

#ifndef __MBARRIER_H__
#define __MBARRIER_H__

/* barriers on amd64 dosn't affect anything and no some specific needed
 * that I cannot said about other arch, leaving it dummy, just to have
 * universal code on other parts.
 * Also, we're need to cpu reorder instructions.
 */

#define barrier_enter()  asm volatile ("" ::: "memory")
#define barrier_leave()  asm volatile ("" ::: "memory")
#define barrier_read()   cpuid_serialization()
#define barrier_write()  asm volatile ("" ::: "memory")

#define memory_barrier()  cpuid_serialization()

static inline void cpuid_serialization(void)
{
  asm volatile ("xorq %%rax, %%rax\n"
		"cpuid\n" ::: "rax", "rbx", "rcx", "rdx", "memory");
}


#endif /* __MBARRIER_H__ */

