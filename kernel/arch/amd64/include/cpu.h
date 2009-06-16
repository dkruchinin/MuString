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
 * (c) Copyright 2008 Dan Kruchinin <dan.kruchinin@gmail.com>
 *
 */

#ifndef __MSTRING_ARCH_CPU_H__
#define __MSTRING_ARCH_CPU_H__

#include <config.h>

/**************
 * CR0 register
 */
#define CR0_PE 0x00000001 /* Protection enabled */
#define CR0_MP 0x00000002 /* Monitor coprocessor */
#define CR0_EM 0x00000004 /* Emulation */
#define CR0_TS 0x00000008 /* Task switched */
#define CR0_ET 0x00000010 /* Extension type */
#define CR0_NE 0x00000020 /* Numeric error */
#define CR0_WP 0x00010000 /* Write protect */
#define CR0_AM 0x00040000 /* Alignment mask */
#define CR0_NW 0x20000000 /* Not writethrough */
#define CR0_CD 0x40000000 /* Cache disable */
#define CR0_PG 0x80000000 /* Paging */

/***************
 * CR3 register
 */
#define CR3_PWT 0x0008 /* Page-Level Writethrough */
#define CR3_PCD 0x0010 /* Page-Level Cache Disable */

#ifdef CONFIG_AMD64
#define CR3_PTABLE_SHIFT 12
#define CR3_PTABLE_MASK  0xffffffffffU
#else /* CONFIG_AMD64 */
#ifndef CONFIG_PAE
#define CR3_PTABLE_SHIFT 12
#define CR3_PTABLE_MASK  0xfffffU
#else /* !CONFIG_PAE */
#define CR3_PTABLE_SHIFT 5
#define CR3_PTABLE_MASK  0x7ffffffU
#endif /* CONFIG_PAE */
#endif /* !CONFIG_AMD64 */

/***************
 * CR4 register
 */
#define CR4_VME        0x0001 /* Virtual-8086 Mode Extensions */
#define CR4_PVI        0x0002 /* Protected-Mode Virtual Interrupts */
#define CR4_TSD        0x0004 /* Time Stamp Disable */
#define CR4_DE         0x0008 /* Debugging Extension */
#define CR4_PSE        0x0010 /* Page Size Extension */
#define CR4_PAE        0x0020 /* Physical-Address Extension */
#define CR4_MCE        0x0040 /* Machine Check Enable */
#define CR4_PGE        0x0080 /* Page-Global Enable */
#define CR4_PCE        0x0100 /* Performance-Monitoring Counter Enable */
#define CR4_OSFXSR     0x0200 /* Operating System FXSAVE/FXRSTOR Support */
#define CR4_OSXMMEXCPT 0x0400 /* Operating System Unmasked Exception Support */

/***************
 * rFLAGS register
 */
#define RFLAGS_CF   0x000001 /* Carry Flag */
#define RFLAGS_PF   0x000004 /* Parity Flag */
#define RFLAGS_AF   0x000010 /* Auxiliary Flag */
#define RFLAGS_ZF   0x000040 /* Zero flag */
#define RFLAGS_SF   0x000080 /* Sign Flag */
#define RFLAGS_TF   0x000100 /* Trap flag */
#define RFLAGS_IF   0x000200 /* Interrupt flag */
#define RFLAGS_DF   0x000400 /* Direction flag */
#define RFLAGS_OF   0x000800 /* Overflow flag */
#define RFLAGS_NT   0x004000 /* Nested Task */
#define RFLAGS_RF   0x010000 /* Resume flag */
#define RFLAGS_VM   0x020000 /* Virtual-8086 Mode */
#define RFLAGS_AC   0x040000 /* Alignment Check */
#define RFLAGS_VIF  0x080000 /* Virtual Interrupt Flag */
#define RFLAGS_VIP  0x100000 /* Virtual Interrupt Pending */
#define RFLAGS_ID   0x200000 /* ID flag */

#define RFLAGS_IOPL_SHIFT 12
#define RFLAGS_IOPL_MASK  0x03

#ifndef __ASM__
#include <arch/msr.h>
#include <mstring/types.h>

struct cpu;
typedef struct arch_cpu {
  uint32_t apic_id;
} arch_cpu_t;

static inline void cpuid(uint32_t *eax, uint32_t *ebx,
                         uint32_t *ecx, uint32_t *edx)
{
  __asm__ volatile ("cpuid\n"                    
                    : "=a" (*eax), "=b" (*ebx),
                      "=c" (*ecx), "=d" (*edx)
                    : "a" (*eax));
}

#define CURRENT_CPU() ((struct cpu *)msr_read(MSR_KERN_GS_BASE))

static inline void arch_set_current_cpu(struct cpu *cur_cpu)
{
  msr_write(MSR_KERN_GS_BASE, (uintptr_t)cur_cpu);
}

#ifdef CONFIG_AMD64
static inline uint64_t rflags_read(void)
{
  uint64_t ret;
  __asm__ volatile ("pushfq\n\t"
                    "popq %0\n"
                    : "=r" (ret));

  return ret;
}

static inline void rflags_write(uint64_t val)
{
  __asm__ volatile ("pushq %0\n\t"
                    "popfq\n"
                    :: "r" (val));
}

#define __READ_CRx(x)                           \
  static inline uint64_t read_cr##x(void)       \
  {                                             \
    uint64_t ret;                               \
    __asm__ volatile ("movq %%cr" #x ", %0\n"   \
                      : "=r" (ret));            \
    return ret;                                 \
  }

#define __WRITE_CRx(x)                          \
  static inline void write_cr##x(uint64_t val)  \
  {                                             \
    __asm__ volatile ("movq %0, %%cr" #x "\n" \
                      :: "r" (val));          \
  }
#else /* CONFIG_AMD64 */
static inline uint32_t rflags_read(void)
{
  uint32_t ret;
  __asm__ volatile ("pushfd\n\t"
                    "popl %0\n"
                    : "=r" (ret));
  return ret;
}

static inline void rflags_write(uint32_t val)
{
  __asm__ volatile ("pushl %0\n\t"
                    "popfd\n"
                    :: "r" (val));
}

#define __READ_CRx(x)                           \
  static inline uint32_t read_cr##x(void)       \
  {                                             \
    uint32_t ret;                               \
    __asm__ volatile ("movl %%cr" #x ", %0\n"   \
                      : "=r" (ret));            \
    return ret;                                 \
  }

#define __WRITE_CRx(x)                          \
  static inline void write_cr##x(uint32_t val)  \
  {                                             \
    __asm__ volatile ("movl %0, %%cr" #x "\n"   \
                      :: "r" (val));            \
  }
#endif /* !CONFIG_AMD64 */

__READ_CRx(0)
__READ_CRx(2)
__READ_CRx(3)
__READ_CRx(4)

__WRITE_CRx(0)
__WRITE_CRx(2)
__WRITE_CRx(3)
__WRITE_CRx(4)

#endif /* !__ASM__ */
#endif /* __MSTRING_ARCH_CPU_H__ */
