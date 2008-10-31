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
 * include/kernel/vm.h: some hardcoded values for user space tasks
 *
 */

#ifndef __KERNEL_VM_H__
#define __KERNEL_VM_H__

#include <eza/arch/types.h>
#include <eza/swks.h>
#include <eza/arch/page.h>

#define USER_END_VIRT 0x10000000000   /* 16 Terabytes */
#define USER_START_VIRT  0x1001000
#define USER_VA_SIZE  (USER_END_VIRT-USER_START_VIRT)

#define USPACE_END       USER_END_VIRT

#define USER_MAX_VIRT  (USER_END_VIRT-SWKS_PAGES*PAGE_SIZE)
#define SWKS_VIRT_ADDR  USER_MAX_VIRT

#define USER_STACK_SIZE  4

status_t copy_to_user(void *dest,void *src,ulong_t size);
status_t copy_from_user(void *dest,void *src,ulong_t size);

static inline bool valid_user_address(uintptr_t addr)
{
  return (addr >= USER_START_VIRT && addr < USER_MAX_VIRT);
}

static inline bool valid_user_address_range(uintptr_t addr,ulong_t size)
{
  if(addr >= USER_START_VIRT && addr < USER_MAX_VIRT) {
    addr += size;
    return (addr >= USER_START_VIRT && addr < USER_MAX_VIRT);
  }
  return false;
}

#endif /* __KERNEL_VM_H__ */

