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
 * x86/x86_64 model-specific registers.
 */

#ifndef __MSTRING_ARCH_MSR__
#define __MSTRING_ARCH_MSR__

/* MSRs */
#define MSR_EFER         0xC0000080
#define MSR_STAR         0xC0000081
#define MSR_LSTAR        0xC0000082
#define MSR_CSTAR        0xC0000083
#define MSR_SF_MASK      0xC0000084
#define MSR_FS_BASE      0xC0000100
#define MSR_GS_BASE      0xC0000101
#define MSR_KERN_GS_BASE 0xC0000102
#define MSR_SYSCFG       0xC0010010
#define MSR_APIC_BAR     0x0000001B /* APIC base address register */

/* EFER features */
#define EFER_SCE   0x00  /* System Call Extensions */
#define EFER_LME   0x08  /* Long Mode Enabled */
#define EFER_LMA   0x0A  /* Long Mode Active */
#define EFER_NXE   0x0B  /* No-Execute enable */
#define EFER_SVME  0x0C  /* Secure Virtual Machine enable */
#define EFER_FFXSR 0x0D  /* Fast FXSAVE/FXRSTOR */

/* APIC base address register stuff */
#define APIC_BAR_ABA_SHIFT 32UL
#define APIC_BAR_ENABLED   (1UL << 11)
#define APIC_BAR_BSC       (1UL << 8)

#ifndef __ASM__
#include <mstring/bitwise.h>
#include <mstring/types.h>

static inline uint64_t msr_read(uint32_t msr)
{
  uint32_t eax, edx;

  __asm__ volatile ("rdmsr\n"
                    : "=a" (eax), "=d" (edx)
                    : "c" (msr));

  return (((uint64_t)edx << 32) | eax);
}

static inline void msr_write(uint32_t msr, uint64_t val)
{
  __asm__ volatile ("wrmsr\n"
                    :: "c" (msr), "a" (val & 0xffffffff),
                       "d" (val >> 32));
}

static inline void efer_set_feature(int ftr_bit)
{
  uint64_t efer = msr_read(MSR_EFER);

  bit_set(&efer, ftr_bit);
  msr_write(MSR_EFER, efer);
}
#endif /* __ASM__ */

#endif /* __MSTRING_ARCH_MSR__ */
