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
 *
 * include/eza/amd64/boot.h: boot constants
 *
 */

#ifndef __BOOT_H__
#define __BOOT_H__

#include <config.h>
#include <eza/arch/page.h>

#define BIOS_END_ADDR 0x100000
#define AP_BOOT_OFFSET   (CONFIG_KCORE_STACK_PAGES << PAGE_WIDTH)
#define BOOT_OFFSET (BIOS_END_ADDR + AP_BOOT_OFFSET)
#define BOOT_STACK_SIZE  0x400

#define MULTIBOOT_HEADER_MAGIC  0x1BADB002
#define MULTIBOOT_HEADER_FLAGS  0x00010003
#define MULTIBOOT_LOADER_MAGIC  0x2BADB002

#endif

