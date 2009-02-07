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
 * (c) Copyright 2008 Michael Tsymbalyuk <mtzaurus@gmail.com>
 *
 * include/eza/amd64/bits.h: Contains types and prototypes for AMD-64 specific
 *                           functions for dealins with bits.
 *
 */

#ifndef __AMD64__BITS_H__
#define __AMD64__BITS_H__ 

#include <eza/bits.h>
#include <eza/arch/types.h>

static inline bit_idx_t find_first_bit32( uint32_t v )
{
  bit_idx_t idx;

   __asm__ volatile(
      "bsf %%ecx, %%edx\n"
  : "=d" (idx) : "c" (v), "d" (INVALID_BIT_INDEX) ); 

  return idx;
}

static inline bit_idx_t find_first_bit64( uint64_t v )
{
  bit_idx_t idx;

  __asm__ volatile(
      "bsf %%rcx, %%rdx\n"
  : "=d" (idx) : "c" (v), "d" (INVALID_BIT_INDEX) );

  return idx;
}

static inline bit_idx_t find_first_bit_mem(long *ptr, long count)
{
  bit_idx_t idx;

  __asm__ volatile(
      "xor %%rax, %%rax\n"
      "0: bsfq (%%rcx), %%rdx\n"
      "jnz 1f\n"
      "add $8, %%rcx\n"
      "add $64, %%rax\n"
      "dec %%rbx\n"
      "jz 2f\n"
      "jmp 0b\n"
      "1: add %%rax, %%rdx\n"
      "2:\n"
  : "=d" (idx) : "b" (count), "c" (ptr), "d" (INVALID_BIT_INDEX) );

  return idx;
}

static inline bit_idx_t reset_and_test_bit_mem(uint64_t *ptr, bit_idx_t bit )
{
  bit_idx_t prev_state;

  __asm__ volatile (
    "mov %%rbx, %%rax\n"
    "and $0x3f, %%rax\n" /* Get offset within a word. */
    "and $0xffffffffffffffc0, %%rbx\n" /* Leave word number. */
    "shr $3, %%rbx\n"   /* Transform offset into dword index. */
    "add %%rbx, %%rdx\n"
    "btr %%rax, (%%rdx)\n"
    "movq $0, %%rax\n"
    "adc $0, %%rax\n"
  : "=r"(prev_state) : "b" (bit), "d" (ptr) );

  return prev_state;
}

static inline bit_idx_t set_and_test_bit_mem(long *ptr, bit_idx_t bit)
{
  bit_idx_t prev_state;

  __asm__ volatile (
    "mov %%rbx, %%rax\n"
    "and $0x3f, %%rax\n" /* Get offset within a word. */
    "and $0xffffffffffffffc0, %%rbx\n" /* Leave word number. */
    "shr $3, %%rbx\n"   /* Transform offset into dword index. */
    "add %%rbx, %%rdx\n"
    "bts %%rax, (%%rdx)\n"
    "movq $0, %%rax\n"
    "adc $0, %%rax\n"
  : "=r"(prev_state) : "b" (bit), "d" (ptr) );

  return prev_state;
}

static inline int count_active_bits(ulong_t p)
{
  int bits=0;
  ulong_t mask=1;

  while( p ) {
    if( p & mask ) {
      bits++;
    }
    p >>= 1;
  }

  return bits;
}

#endif

