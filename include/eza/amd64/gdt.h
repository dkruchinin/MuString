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
 * include/eza/gdt.c: default Global Descriptor Table entries live here.
 *
 */

#include <eza/arch/page.h>

#define GDT_CPU_ENTRIES  { \
   { 0,0,0,0,0,0,0,0,0,0 }, /* nil descriptor */ \
  /* kernel .text descriptor */ \
  { .limit_0_15=0xffff, \
    .base_0_15=0, \
    .base_16_23=0, \
    .access=AR_PRESENT | AR_CODE | DPL_KERNEL | AR_READABLE, \
    .limit_16_19=0xf, \
    .available=0, \
    .longmode=1, \
    .special=0, \
    .granularity=1, \
    .base_24_31=0}, \
  /* kernel .data descriptor */ \
  { .limit_0_15=0xffff, \
    .base_0_15=0, \
    .base_16_23=0, \
    .access=AR_PRESENT | AR_DATA | AR_WRITEABLE | DPL_KERNEL, \
    .limit_16_19=0xf, \
    .available=0, \
    .longmode=1, \
    .special=0, \
    .granularity=1, \
    .base_24_31=0}, \
  /* user .data descriptor */ \
  { .limit_0_15=0xffff, \
    .base_0_15=0, \
    .base_16_23=0, \
    .access=AR_PRESENT | AR_DATA | AR_WRITEABLE | (1 << 2) | DPL_USPACE, \
    .limit_16_19=0xf, \
    .available=0, \
    .longmode=1, \
    .special=1, \
    .granularity=1, \
    .base_24_31=0}, \
  /* user .text descriptor */ \
  { .limit_0_15=0xffff, \
    .base_0_15=0, \
    .base_16_23=0, \
    .access=AR_PRESENT | AR_CODE | DPL_USPACE, \
    .limit_16_19=0xf, \
    .available=0, \
    .longmode=1, \
    .special=0, \
    .granularity=1, \
    .base_24_31=0}, \
  /* kernel .text 32 bit mode - while we're on protected mode*/ \
  { .limit_0_15=0xffff, \
    .base_0_15=0, \
    .base_16_23=0, \
    .access=AR_PRESENT | AR_CODE | DPL_KERNEL | AR_READABLE, \
    .limit_16_19=0xf, \
    .available=0, \
    .longmode=0, \
    .special=1, \
    .granularity=1, \
    .base_24_31=0}, \
  /* tss desciptor - for 64bit on amd64 we're need two descriptors */ \
  { 0,0,0,0,0,0,0,0,0,0 }, \
  { 0,0,0,0,0,0,0,0,0,0 }, \
  /* LDT descriptor - need to expand it by extra 64 bits */ \
  { 0,0,0,0,0,0,0,0,0,0 }, \
  { 0,0,0,0,0,0,0,0,0,0 }, \
}
