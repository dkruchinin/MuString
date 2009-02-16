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
 * (c) Copyright 2005,2008 Tirra <madtirra@jarios.org>
 *
 * include/kernel/elf.h: elf defines
 *
 */


#ifndef __KERNEL_ELF_H__
#define __KERNEL_ELF_H__

#include <eza/arch/types.h>

#define EI_NIDENT  16

/* sections flags */
#define ESH_WRITE  0x1 /* writable */
#define ESH_ALLOC  0x2 /* allocatable */
#define ESH_EXEC   0x4 /* executable */
/* section types */
#define SHT_NULL           0 /* Section header table entry unused */
#define SHT_PROGBITS       1 /* Program specific (private) data */
#define SHT_SYMTAB         2 /* Link editing symbol table */
#define SHT_STRTAB         3 /* A string table */
#define SHT_RELA           4 /* Relocation entries with addends */
#define SHT_HASH           5 /* A symbol hash table */
#define SHT_DYNAMIC        6 /* Information for dynamic linking */
#define SHT_NOTE           7 /* Information that marks file */
#define SHT_NOBITS         8 /* Section occupies no space in file */
#define SHT_REL            9 /* Relocation entries, no addends */
#define SHT_SHLIB          10 /* Reserved, unspecified semantics */
#define SHT_DYNSYM         11 /* Dynamic linking symbol table */
#define SHT_INIT_ARRAY     14 /* Array of ptrs to init functions */
#define SHT_FINI_ARRAY     15 /* Array of ptrs to finish functions */
#define SHT_PREINIT_ARRAY  16 /* Array of ptrs to pre-init funcs */
#define SHT_GROUP          17 /* Section contains a section group */
#define SHT_SYMTAB_SHNDX   18 /* Indicies for SHN_XINDEX entries */

#define ELF_MAGIC  0x464C457F

/* elf image header */
typedef struct __elf64_t 
{
  unsigned char e_ident[EI_NIDENT]; /* ELF64 magic number */
  uint16_t e_type; /* elf type */
  uint16_t e_machine; /* elf required architecture */
  uint32_t e_version; /* elf object file version */
  uintptr_t e_entry; /* entry point virtual address */
  uintptr_t e_phoff; /* program header file offset */
  uintptr_t e_shoff; /* section header file offset */
  uint32_t e_flags; /* processor specific object tags */
  uint16_t e_ehsize; /* elf header size in bytes */
  uint16_t e_phentsize; /* program header table entry size */
  uint16_t e_phnum; /* program header count */
  uint16_t e_shentsize; /* section header table entry size */
  uint16_t e_shnum; /* section header count */
  uint16_t e_shstrndx; /* section header string table index */
} elf64_t;

/* program header */
typedef struct {
  uint32_t p_type; /* program segment type */
  uint32_t p_flags; /* segment flags */
  uintptr_t p_offset; /* segment image offset */
  uintptr_t p_vaddr; /* segment virtual address */
  uintptr_t p_paddr; /* segment physical address */
  size_t p_filesz; /* segment size on image */
  size_t p_memsz; /* segment size on memory */
  size_t p_align; /* segment alignment on image and memory */
} elf64_pr_t;

/* section header */
typedef struct {
  uint32_t sh_name; /* section name, actually is a index in string table */
  uint32_t sh_type; /* type of a section */
  uint64_t sh_flags; /* section flags */
  uintptr_t sh_addr; /* section virtual address at execution */
  uintptr_t sh_offset; /* section image offset */
  ulong_t sh_size; /* size of section in bytes */
  uint32_t sh_link; /* index of another section */
  uint32_t sh_info; /* additional section information */
  ulong_t sh_addralign; /* section alignment */
  size_t sh_entsize; /* entry size if section holds table*/
} elf64_sh_t;

#endif /* __KERNEL_ELF_H__*/
