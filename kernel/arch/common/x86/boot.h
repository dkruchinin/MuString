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
 *
 * Multiboot header and x86 e820 map definitions.
 */

#ifndef __MSTRING_ARCH_BOOT_H__
#define __MSTRING_ARCH_BOOT_H__

#define MULTIBOOT_HEADER_MAGIC  0x1BADB002
#define MULTIBOOT_HEADER_FLAGS  0x00010003
#define MULTIBOOT_LOADER_MAGIC  0x2BADB002

#ifndef __ASM__
#include <arch/types.h>

typedef struct multiboot_info {
  uint32_t flags;
  uint32_t mem_lower;     /* amount of lower memory 0M - 640M */
  uint32_t mem_upper;     /* amount of upper memory 1G - NG */
  uint32_t boot_device;   /* indicates BIOS disk device */
  uint32_t cmdline;       /* physical address of the passed kernel comand line */
  uint32_t mods_count;    /* number of loaded modules */
  uint32_t mods_addr;     /* physical address of first module structure */
  uint32_t syms[4];     
  uint32_t mmap_length;   /* size of memory map buffer */
  uint32_t mmap_addr;     /* physical address of memory map buffer */
} __attribute__ ((packed)) multiboot_info_t;

typedef enum e820_mem_type {
  E820_USABLE   = 1,
  E820_RESERVED = 2,
  E820_ACPI     = 3,
  E820_NVS      = 4,
  E820_BAD      = 5,
} e820_mem_type_t;

typedef struct {
  uint32_t size;
  uint64_t base_address;
  uint64_t length;
  uint32_t type;
} __attribute__ ((packed)) e820memmap_t;

extern uint32_t multiboot_info_ptr;
extern uint32_t multiboot_magic;
extern multiboot_info_t *mb_info;

#endif /* __ASM__ */
#endif /* __MSTRING_ARCH_BOOT_H__ */
