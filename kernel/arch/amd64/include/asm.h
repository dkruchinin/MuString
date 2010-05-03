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
 * include/mstring/amd64/asm.h: generic assembler functions
 *
 */

#ifndef __MSTRING_ARCH_ASM_H__
#define __MSTRING_ARCH_ASM_H__

#include <config.h>
#include <arch/page.h>
#include <mstring/types.h>

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
    volatile uint8_t val;
    __asm__ volatile("inb %%dx, %%al\n"
                     : "=a" (val)
                     : "d" (port));

    return val;
}


static inline void outb(uint16_t port, uint8_t val)
{
    __asm__ volatile ("outb %%al, %%dx\n"
                      :: "a" (val), "d" (port));
}

static inline void gdtr_load(struct table_reg *gdtr_reg)
{
  asm volatile("lgdtq %0\n" : : "m" (*gdtr_reg));
}

static inline void ldtr_load(long ldtr_reg)
{
  asm volatile( "lldt %0\n" : : "r"((short)ldtr_reg));
}

static inline void gdtr_store(struct table_reg *gdtr_reg)
{
  asm volatile("sgdtq %0\n" : : "m" (*gdtr_reg));
}

static inline void idtr_load(struct table_reg *idtr_reg)
{
  asm volatile("lidtq %0\n" : : "m" (*idtr_reg));
}

static inline void tr_load(uint16_t s)
{
  asm volatile("ltr %0" : : "r" (s));
}

static inline void fxsave(uintptr_t saveptr)
{
    __asm__ volatile ("fxsave (%1)\n"
                      : "=m" (*(long*)saveptr)
                      : "r" (saveptr));
}

static inline void fxrstor(uintptr_t resaddr)
{
    __asm__ volatile ("fxrstor (%%rdx)\n"
                      :: "d" (resaddr));
}

/* Load RSP with a given value. It MUST NOT be a function since after
 * stack switch it won't be possible to return.
 */
#define load_stack_pointer(sp) \
  __asm__ volatile (\
     "mov %%rax,%%rsp\n" \
     :: "a" (sp) )


#endif /* __MSTRING_ARCH_ASM_H__ */

