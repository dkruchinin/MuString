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
 * (c) Copyright 2009 Dan Kruchinin <dan.kruchinin@gmail.com>
 *
 * x86/x86_64 CPUID functions and definitions
 */

#ifndef __MSTRING_ARCH_CPUFEATURES_H__
#define __MSTRING_ARCH_CPUFEATURES_H__

#include <mstring/types.h>
#include <mstring/bitwise.h>
#include <arch/cpu.h>

#define GET_FTRS_CMD_32  1
#define GET_FTRS_CMD_64  0x80000001

#define ECX32_FTR_BASE 0
#define EDX32_FTR_BASE 32
#define ECX64_FTR_BASE 64
#define EDX64_FTR_BASE 96

#define FTR_CNT(reg, bits, cnt)                 \
    (reg##bits##_FTR_BASE + (cnt))

/* EAX = 1, x86 ECX feature identifiers */
#define X86_FTR_SSE3        FTR_CNT(ECX, 32, 00) /* SSE3 extension */
#define X86_FTR_PCLMULQDQ   FTR_CNT(ECX, 32, 01) /* PCLMULQDQ instruction */
#define X86_FTR_DTES64      FTR_CNT(ECX, 32, 02) /* 64-bit DS area */
#define X86_FTR_MONITOR     FTR_CNT(ECX, 32, 03) /* MONITOR/MWAIT instructions. */
#define X86_FTR_DSCPL       FTR_CNT(ECX, 32, 04) /* CPL Qualified Debug Store */
#define X86_FTR_VMX         FTR_CNT(ECX, 32, 05) /* Virtual Machine Extension */
#define X86_FTR_SMX         FTR_CNT(ECX, 32, 06) /* Safer Mode Extension */
#define X86_FTR_EST         FTR_CNT(ECX, 32, 07) /* Enhanced Intel SpeedStep technology */
#define X86_FTR_TM2         FTR_CNT(ECX, 32, 08) /* Thermal Monitor 2. */
#define X86_FTR_SSSE3       FTR_CNT(ECX, 32, 09) /* Streaming SIMD Extension 3 */
#define X86_FTR_CNXT_ID     FTR_CNT(ECX, 32, 10) /* L1 Context ID. */
#define X86_FTR_CMPXCHG16   FTR_CNT(ECX, 32, 13) /* CMPXCHG16B instruction. */
#define X86_FTR_xTPRUPDCNTL FTR_CNT(ECX, 32, 14) /* xTPR Update Control */
#define X86_FTR_PDCM        FTR_CNT(ECX, 32, 15) /* Performance and Debug Capability */
#define X86_FTR_DCA         FTR_CNT(ECX, 32, 18) /* Data prefetching from memory mapped device */
#define X86_FTR_SSE41       FTR_CNT(ECX, 32, 19) /* SSE4.1 support */
#define X86_FTR_SSE42       FTR_CNT(ECX, 32, 20) /* SSE4.2 support */
#define X86_FTR_x2APIC      FTR_CNT(ECX, 32, 21) /* x2APIC support */
#define X86_FTR_MOVBE       FTR_CNT(ECX, 32, 22) /* MOVBE instruction */
#define X86_FTR_POPCNT      FTR_CNT(ECX, 32, 23) /* POPCNT instruction */
#define X86_FTR_AES         FTR_CNT(ECX, 32, 25) /* AES instruction extension */
#define X86_FTR_XSAVE       FTR_CNT(ECX, 32, 26) /* XSAVE/XRSTOR, XSETBV/XGETBV. XFEATURE_ENABLD_MASK */
#define X86_FTR_OSXSAVE     FTR_CNT(ECX, 32, 27) /* XSAVE enabled */

/* EAX = 1, x86 EDX feature identifier */
#define X86_FTR_FPU         FTR_CNT(EDX, 32,  0) /* Floating point unit on-chip */
#define X86_FTR_VME         FTR_CNT(EDX, 32,  1) /* Virtual 8086 Mode Enhancement */
#define X86_FTR_DE          FTR_CNT(EDX, 32,  2) /* Debugging extension */
#define X86_FTR_PSE         FTR_CNT(EDX, 32,  3) /* Page size extension */
#define X86_FTR_TSC         FTR_CNT(EDX, 32,  4) /* Time Stamp Counter */
#define X86_FTR_MSR         FTR_CNT(EDX, 32,  5) /* Model Specified Registers */
#define X86_FTR_PAE         FTR_CNT(EDX, 32,  6) /* Physical Address Extension */
#define X86_FTR_MCE         FTR_CNT(EDX, 32,  7) /* Machine Check Exception */
#define X86_FTR_CX8         FTR_CNT(EDX, 32,  8) /* CMPXCHG8B instruction */
#define X86_FTR_APIC        FTR_CNT(EDX, 32,  9) /* APIC on-chip */
#define X86_FTR_SEP         FTR_CNT(EDX, 32, 11) /* SYSENTER/SYSEXIT instructions */
#define X86_FTR_MTTR        FTR_CNT(EDX, 32, 12) /* Memory Type Range Registers */
#define X86_FTR_PGE         FTR_CNT(EDX, 32, 13) /* PTE global bit */
#define X86_FTR_MCA         FTR_CNT(EDX, 32, 14) /* Machine check architecture */
#define X86_FTR_CMOV        FTR_CNT(EDX, 32, 15) /* Conditional Mode instructions */
#define X86_FTR_PAT         FTR_CNT(EDX, 32, 16) /* Page Attribute Table */
#define X86_FTR_PSE36       FTR_CNT(EDX, 32, 17) /* 36-bit Page Size Extension */
#define X86_FTR_PSN         FTR_CNT(EDX, 32, 18) /* Processor Serial Number */
#define X86_FTR_CLFSH       FTR_CNT(EDX, 32, 19) /* CLFLUSH instruction. */
#define X86_FTR_DS          FTR_CNT(EDX, 32, 21) /* Debug Store */
#define X86_FTR_ACPI        FTR_CNT(EDX, 32, 22) /* Thermal Monitor and Software Controlled Clock */
#define X86_FTR_MMX         FTR_CNT(EDX, 32, 23) /* Intel MMX Technology */
#define X86_FTR_FXSR        FTR_CNT(EDX, 32, 24) /* FXSAVE and FXRSTOR Instructions */
#define X86_FTR_SSE         FTR_CNT(EDX, 32, 25) /* SSE extensions */
#define X86_FTR_SSE2        FTR_CNT(EDX, 32, 26) /* SSE2 extensions */
#define X86_FTR_SS          FTR_CNT(EDX, 32, 27) /* Self Snoop. */
#define X86_FTR_HTT         FTR_CNT(EDX, 32, 28) /* Multi-threading */
#define X86_FTR_TM          FTR_CNT(EDX, 32, 29) /* Thermal Monitor */
#define X86_FTR_PBE         FTR_CNT(EDX, 32, 31) /* Pending Break Enable. */

/* EAX = 0x80000001, ECX feature identifiere */
#define AMD64_FTR_LAHFSAHF  FTR_CNT(ECX, 64, 00) /* LAHF/SAHF instruction suppoert in 64bit mode */
#define AMD64_FTR_CMPLEGACY FTR_CNT(ECX, 64, 01) /* Core multi-processig legacy mode */
#define AMD64_FTR_SVM       FTR_CNT(ECX, 64, 02) /* Secure Virtual Machine */
#define AMD64_FTR_EAPICSPC  FTR_CNT(ECX, 64, 03) /* Extended APIC space */
#define AMD64_FTR_ALTMOVCR8 FTR_CNT(ECX, 64, 04) /* LOCK MOV CR0 means MOV CR8 */
#define AMD64_FTR_ABM       FTR_CNT(ECX, 64, 05) /* Advanced bit manipulation */
#define AMD64_FTR_SSE4A     FTR_CNT(ECX, 64, 06) /* EXTRQ/INSERTQ/ETC instructions */
#define AMD64_FTR_MISALGSSE FTR_CNT(ECX, 64, 07) /* Misaligned SSE mode */
#define AMD64_FTR_3DNOWPRF  FTR_CNT(ECX, 64, 08) /* PREFETCH/PREFETCHW instructions */
#define AMD64_FTR_OSVW      FTR_CNT(ECX, 64, 09) /* OS visible workaround */
#define AMD64_FTR_IBS       FTR_CNT(ECX, 64, 10) /* Instruction base sampling */
#define AMD64_FTR_SSE5      FTR_CNT(ECX, 64, 11) /* SSE5 instructions support */
#define AMD64_FTR_SKINIT    FTR_CNT(ECX, 64, 12) /* SKINIT/STGI instructions */
#define AMD64_FTR_WDT       FTR_CNT(ECX, 64, 13) /* Watchdog timer support */

/* EAX = 0x80000001, EDX feature identifiere */
#define AMD64_FTR_SYSCSR    FTR_CNT(EDX, 64, 11) /* SYSCALL/SYSRET instructions */
#define AMD64_FTR_NX        FTR_CNT(EDX, 64, 20) /* no-execute page protection */
#define AMD64_FTR_MMXEXT    FTR_CNT(EDX, 64, 22) /* AMD extentions to MMX instructions */
#define AMD64_FTR_FFXSR     FTR_CNT(EDX, 64, 25) /* FXSAVE/FXRSROR instructions optimization */
#define AMD64_FTR_PAGE1G    FTR_CNT(EDX, 64, 26) /* 1GB large page support */
#define AMD64_FTR_RDTSCP    FTR_CNT(EDX, 64, 27) /* RDTSCP instruction */
#define AMD64_FTR_LM        FTR_CNT(EDX, 64, 29) /* Long-Mode support */
#define AMD64_FTR_3DNOWEXT  FTR_CNT(EDX, 64, 30) /* AMD extensions to 3DNow! */
#define AMD64_FTR_3DNOW     FTR_CNT(EDX, 64, 31) /* 3DNow! onstructions */

/**
 * Determines if CPU has feature @a feature.
 * @param feature - Index of feature to test.
 * @return True if feature is supported by CPU, False otherwise.
 */
static inline bool cpu_has_feature(uint32_t feature)
{
  uint32_t eax = GET_FTRS_CMD_32;
  uint32_t ebx, ecx, edx;

  if (feature >= ECX64_FTR_BASE) {
    eax = GET_FTRS_CMD_64;
  }

  cpuid(&eax, &ebx, &ecx, &edx);
  if (feature < EDX32_FTR_BASE) {
    return bit_test(&ecx, feature);
  }
  else if (feature < ECX64_FTR_BASE) {
    return bit_test(&edx, feature - EDX32_FTR_BASE);
  }
  else if (feature < EDX64_FTR_BASE) {
    return bit_test(&ecx, feature - ECX64_FTR_BASE);
  }
  else {
    return bit_test(&edx, feature - EDX64_FTR_BASE);
  }
}

#endif /* __MSTRING_ARCH_CPUFEATURES_H__ */
