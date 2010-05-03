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
 * (c) Copyright 2009 Dmitry Gromada <gromada@jarios.org>
 *
 * amd64 arch depended declarations for process tracing
 */

#ifndef __ARCH_PTRACE_H__
#define __ARCH_PTRACE_H__

#include <arch/types.h>

struct ptrace_regs {
  ulong_t rax;
  ulong_t rbx;
  ulong_t rcx;
  ulong_t rdx;
  ulong_t rsi;
  ulong_t rdi;
  ulong_t rbp;
  ulong_t rsp;
  ulong_t r8;
  ulong_t r9;
  ulong_t r10;
  ulong_t r11;
  ulong_t r12;
  ulong_t r13;
  ulong_t r14;
  ulong_t r15;
  ulong_t rip;
  unsigned int eflags;
  unsigned int cs;
  unsigned int ss;
  unsigned int ds;
  unsigned int es;
  unsigned int fs;
  unsigned int gs;
  long double st0;
  long double st1;
  long double st2;
  long double st3;
  long double st4;
  long double st5;
  long double st6;
  long double st7;
  unsigned int fctrl;
  unsigned int fstat;
  unsigned int ftag;
  unsigned int fiseg;
  unsigned int fioff;
  unsigned int foseg;
  unsigned int fooff;
  unsigned int fop;
  uint8_t xmm0[16];
  uint8_t xmm1[16];
  uint8_t xmm2[16];
  uint8_t xmm3[16];
  uint8_t xmm4[16];
  uint8_t xmm5[16];
  uint8_t xmm6[16];
  uint8_t xmm7[16];
  uint8_t xmm8[16];
  uint8_t xmm9[16];
  uint8_t xmm10[16];
  uint8_t xmm11[16];
  uint8_t xmm12[16];
  uint8_t xmm13[16];
  uint8_t xmm14[16];
  uint8_t xmm15[16];
  unsigned int mxcsr;
};

int arch_ptrace(ptrace_cmd_t cmd, task_t *child, uintptr_t addr, void *data);
void arch_ptrace_cont(task_t *child);

/* register definition */

#endif /* __ARCH_PTRACE_H__ */
