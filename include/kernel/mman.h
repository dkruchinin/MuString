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
 * Originally written by MadTirra <madtirra@jarios.org>
 * (c) Copyright 2006,2007,2008 MString Core Team <http://mstring.jarios.org>
 *
 * include/kernel/mman.h: some hardcoded values for mmap syscall
 *
 */

#ifndef __KERNEL_MMAN_H__
#define __KERNEL_MMAN_H__

#include <eza/arch/types.h>

#define NOFD  (1 << 24)

#define MMAP_NONCACHABLE  0x01
#define MMAP_RDONLY       0x02
#define MMAP_RW           0x04
#define MMAP_PHYS         0x08


#endif /* __KERNEL_MMAN_H__ */

