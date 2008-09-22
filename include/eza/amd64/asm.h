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
 * (c) Copyright 2008 Michael Tsymbalyuk <mtzaurus@gmail.com>
 *                (added CR3-related functions)
 * (c) Copyright 2008 Dan Kruchinin <dan.kruchinin@gmail.com>
 *                (add lock prefix)
 *
 * include/eza/amd64/asm.h: generic assembler functions
 *
 */

#ifndef __ASM_H__
#define __ASM_H__

#include <config.h>
#include <eza/arch/types.h>
#include <eza/arch/mm_types.h>
#include <eza/arch/page.h>

#ifdef CONFIG_SMP
/*
 * x86 and x86_64(amd64) architectures provide lock prefix
 * that guaranty atomic execution limited set of operations
 * such as:
 * ADC, ADD, AND, BTC, BTR, BTS, CMPXCHG, CMPXCHG8B, CMPXCHG16B, DEC,
 * INC, NEG, NOT, OR, SBB, SUB, XADD, XCHG, and XOR
 * (list of operations supporting lock prefix was taken from amd64 manual,
 * volume 3)
 */
#define __LOCK_PREFIX "lock "
#else
#define __LOCK_PREFIX ""
#endif /* CONFIG_SMP */

extern void set_efer_flag(int flag);

extern void arch_delay_loop(uint32_t v);
extern void arch_fake_loop(uint32_t v);

static inline uintptr_t get_stack_base(void)
{
  uintptr_t stack;

  asm volatile("andq %%rsp, %0\n" : "=r" (stack) : "0" (~((uint64_t)(((1 << 0) * PAGE_SIZE))-1)));

  return stack;
}

static inline uint8_t inb(uint16_t port)
{
    uint8_t val;

    __asm__ volatile("inb %1, %0\n"
                     : "=a" (val)
                     : "d" (port));

    return val;
}


static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile ("outb %0, %1\n"
                      : : "a" (val), "Nd" (port));
}

static inline void gdtr_load(struct __ptr_16_64 *gdtr_reg)
{
  asm volatile("lgdtq %0\n" : : "m" (*gdtr_reg));
}

static inline void gdtr_store(struct __ptr_16_64 *gdtr_reg)
{
  asm volatile("sgdtq %0\n" : : "m" (*gdtr_reg));
}

static inline void idtr_load(struct __ptr_16_64 *idtr_reg)
{
  asm volatile("lidtq %0\n" : : "m" (*idtr_reg));
}

static inline void tr_load(uint16_t s)
{
  asm volatile("ltr %0" : : "r" (s));
}

/* interrupts functions */

/* interrupts_enable(): enabling interrupts and return
 * previous EFLAGS value.
 */
static inline ipl_t interrupts_enable(void)
{
  ipl_t o;

  __asm__ volatile (
		    "pushfq\n"
		    "popq %0\n"
		    "sti\n"
		    : "=r" (o)
		    );

  return o;
}

/* interrupts_disable(): return the same as interrupts_enable()
 * disabling interrupts.
 */
static inline ipl_t interrupts_disable(void)
{
  ipl_t o;

  __asm__ volatile (
		    "pushfq\n"
		    "popq %0\n"
		    "cli\n"
		    : "=r" (o)
		    );

  return o;
}

/* restore interrupts priority, i.e. restore EFLAGS */
static inline void interrupts_restore(ipl_t b)
{
  __asm__ volatile (
		    "pushq %0\n"
		    "popfq\n"
		    : : "r" (b)
		    );
}

static inline ipl_t interrupts_read(void)
{
  ipl_t o;

  __asm__ volatile (
		    "pushfq\n"
		    "popq %0\n"
		    : "=r" (o)
		    );

  return o;
}

/* MSR and others */

/* write msr */
static inline void write_msr(uint32_t msr,uint64_t v)
{
  __asm__ volatile (
		    "wrmsr;" : : "c" (msr),"a" ((uint32_t) v),"d" ((uint32_t)(v >> 32))
		    );
}

/* just read msr */
static inline unative_t read_msr(uint32_t msr)
{
  uint32_t ax,dx;

  __asm__ volatile ("rdmsr;" : "=a" (ax), "=d" (dx) : "c" (msr));

  return ((uint64_t)dx << 32) | ax;
}

/* CR3 management. See manual for details about 'PCD' and 'PWT' fields. */
static inline void load_cr3( uintptr_t phys_addr, uint8_t pcd, uint8_t pwt )
{
  uintptr_t cr3_val = ( ((pwt & 1) << 3) | ((pcd & 1) << 4) );

  /* Normalize new PML4 base. */
  phys_addr >>= 12;

  /* Setup 20 lowest bits of the PML4 base. */
  cr3_val |= ((phys_addr & 0xfffff) << 12);

  /* Setup highest 20 bits of the PML4 base. */
  cr3_val |= ((phys_addr & (uintptr_t)0xfffff00000) << 12);

 __asm__ volatile(  "movq %%rax, %%cr3" :: "a" (cr3_val) );
}

/* Load RSP with a given value. It MUST NOT be a function since after
 * stack switch it won't be possible to return.
 */
#define load_stack_pointer(sp) \
  __asm__ volatile (\
     "mov %%rax,%%rsp\n" \
     :: "a" (sp) )

#endif /* __ASM_H__ */

