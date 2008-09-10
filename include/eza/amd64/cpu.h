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
 * (c) Copyright 2005,2008 Tirra <tirra.newly@gmail.com>
 * (c) Copyright 2008 Dan Kruchinin <dan.kruchinin@gmail.com>
 *
 * include/eza/amd64/cpu.h: defines used for cpu and cpuid functionality, also
 *                          used in bootstrap and cpu identification
 *
 */

#ifndef __AMD64_CPU_H__
#define __AMD64_CPU_H__

#include <config.h>

/* specific amd flags and registers */
#define AMD_MSR_STAR    0xc0000081 /* msr */
#define AMD_MSR_LSTAR   0xc0000082
#define AMD_MSR_SFMASK  0xc0000084
#define AMD_MSR_FS      0xc0000100
#define AMD_MSR_GS      0xc0000101
#define EFER_MSR_NUM    0xc0000080
/* flags */
#define AMD_SCE_FLAG    0
#define AMD_LME_FLAG    8
#define AMD_LMA_FLAG    10
#define AMD_FFXSR_FLAG  14
#define AMD_NXE_FLAG    11

#define RFLAGS_IF  (1<<9)
#define RFLAGS_RF  (1<<16)

/* cpuid stuff */
/* feature flags */
#define AMD_CPUID_EXTENDED   0x80000001 /* amd64 */
#define AMD_EXT_NOEXECUTE    20
#define AMD_EXT_LONG_MODE    29
#define INTEL_CPUID_STANDARD 0x00000001 /* intel */
#define INTEL_CPUID_EXTENDED 0x80000000
#define INTEL_SSE2           26
#define INTEL_FXSAVE         24

#ifndef __ASM__

#ifdef CONFIG_SMP
#define __percpu__ __attribute__((__section__(".percpu_data")))
#else
#define __percpu__
#endif /* CONFIG_SMP */

/* varios CPU functions from cpu.c */
void cpu_setup_fpu(void);

#endif /* __ASM__ */

#endif /* __AMD64_CPU_H__ */

