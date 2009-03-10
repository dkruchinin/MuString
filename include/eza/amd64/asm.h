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
 * (c) Copyright 2008 Michael Tsymbalyuk <mtzaurus@gmail.com>
 *                (added CR3-related functions)
 * (c) Copyright 2009 Dan Kruchinin
 *
 * include/eza/amd64/asm.h: generic assembler functions
 *
 */

#ifndef __ASM_H__
#define __ASM_H__

#include <config.h>
#include <eza/arch/page.h>
#include <eza/arch/cpu.h>
#include <eza/arch/ptable.h>
#include <mlibc/types.h>

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

static inline void ldtr_load(long ldtr_reg)
{
  asm volatile( "lldt %0\n" : : "r"((short)ldtr_reg));
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

/* MSR and others */

/* write msr */
static inline void write_msr(uint32_t msr,uint64_t v)
{
  __asm__ volatile (
		    "wrmsr;" : : "c" (msr),"a" ((uint32_t) v),"d" ((uint32_t)(v >> 32))
		    );
}

/* just read msr */
static inline uint64_t read_msr(uint32_t msr)
{
  uint32_t ax,dx;

  __asm__ volatile ("rdmsr;" : "=a" (ax), "=d" (dx) : "c" (msr));

  return ((uint64_t)dx << 32) | ax;
}

/* Load RSP with a given value. It MUST NOT be a function since after
 * stack switch it won't be possible to return.
 */
#define load_stack_pointer(sp) \
  __asm__ volatile (\
     "mov %%rax,%%rsp\n" \
     :: "a" (sp) )

/* CR3 management. See manual for details about 'PCD' and 'PWT' fields. */
#if 0
static inline void load_cr3(uintptr_t phys_addr, uint8_t pcd, uint8_t pwt)
{
  uintptr_t cr3_val = (((pwt & 1) << 3) | ((pcd & 1) << 4));
  
  /* Normalize new PML4 base. */
  phys_addr >>= PAGE_WIDTH;

  /* Setup 20 lowest bits of the PML4 base. */
  cr3_val |= ((phys_addr & 0xfffff) << PAGE_WIDTH);

  /* Setup highest 20 bits of the PML4 base. */
  cr3_val |= ((phys_addr & (uintptr_t)0xfffff00000) << PAGE_WIDTH);
  
  __asm__ volatile("movq %0, %%cr3" :: "r" (cr3_val));
}
#endif

static inline long read_cr3(void)
{
  long ret;
  __asm__ volatile("movq %%cr3, %0\n\t"
                   : "=r" (ret));

  return ret;
}

static inline void write_cr3(long val)
{
  __asm__ volatile("movq %0, %%cr3\n\t"
                   :: "r" (val));
}

struct __pde;

static inline void load_cr3(struct __pde *pde)
{
  write_cr3(k2p(pde));
}

#endif /* __ASM_H__ */

