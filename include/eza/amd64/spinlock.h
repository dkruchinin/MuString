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
 * include/eza/amd64/spinlock.h: spinlock and atomic amd64 specific and 
 *                               extended functions
 *
 */

#ifndef __AMD64_SPINLOCK_H__
#define __AMD64_SPINLOCK_H__

#include <eza/arch/types.h>
#include <eza/arch/mbarrier.h>
#include <eza/arch/asm.h>


#define arch_spinlock_lock(s)                   \
    __asm__ __volatile__(  "movl %2,%%eax\n" \
                           "1:" __LOCK_PREFIX "cmpxchgl %0, %1\n"       \
                           "cmp %2, %%eax\n"                            \
                           "jnz 1b\n"                                   \
                           :: "r"(__SPINLOCK_LOCKED_V),"m"( *(&(((spinlock_t*)s)->__spin_val)) ), "rI"(__SPINLOCK_UNLOCKED_V) \
                           : "%rax", "memory" )

#define arch_spinlock_unlock(s) \
    __asm__ __volatile__( __LOCK_PREFIX "xchgl %0, %1\n"        \
                          :: "r"(__SPINLOCK_UNLOCKED_V), "m"( *(&(((spinlock_t*)s)->__spin_val)) ) \
                          : "memory" ) \

#endif /* __AMD64_SPINLOCK_H__ */

