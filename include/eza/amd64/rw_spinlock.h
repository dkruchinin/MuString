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
 * (c) Copyright 2008 Michael Tsymbalyuk <mtzaurus@gmail.com>
 *
 * include/eza/rw_spinlock.h: x86-64 implementation of read-write spinlocks.
 *
 */


#ifndef __AMD64_RW_SPINLOCK__
#define __AMD64_RW_SPINLOCK__

#include <eza/arch/types.h>
#include <eza/arch/mbarrier.h>
#include <eza/arch/asm.h>

/* Main strategy of JariOS read-write spinlocks.
 * We use two counters: one for counting readers and one for counting
 * writers. There can be only 0 or 1 writers, but unlimited amount of
 * readers, so we can use bits [1 .. N] of the 'writers' counter for
 * our needs. We deal with RW spinlocks in two steps. First, we implement
 * a pure spinlock (to access these two variables atomically) via 'btr'
 * instruction. After we've grabbed the first lock, we compare its value
 * against 256, since we use 8th bit of the 'writers' counter - since
 * there can be only 0 or 1 writers, we will always get either 256 or
 * 257 in this counter after setting 8th bit (after locking the lock).
 * So if we have 257, this means that our RW lock has been grabbed
 * by a writer and we must release the bit and repeat the procedure
 * from the beginning waiting for the writer to decrement the counter.
 */

#define arch_rw_spinlock_lock_read(s)           \
    __asm__ __volatile__( "0: movq %0,%%rax\n"  \
    "1:" __LOCK_PREFIX "bts %%rax,%1\n"         \
    "jc 1b\n"                                   \
    "cmpl $256, %1\n"                           \
    "je 3f\n"                                   \
    __LOCK_PREFIX "btr %%rax,%1\n"              \
    "jmp 0b\n"                                  \
    "3: " __LOCK_PREFIX "incl %2\n"             \
    "4: " __LOCK_PREFIX "btr\r %%rax,%1\n"    \
    :: "rI"(8), "m"(*(&(((rw_spinlock_t*)s)->__w))), \
    "m"( *(&(((rw_spinlock_t*)s)->__r) ) ) : "%rax", "memory" )

#define arch_rw_spinlock_lock_write(s)           \
    __asm__ __volatile__( "0: movq %0,%%rax\n"  \
    "1:" __LOCK_PREFIX "bts %%rax,%1\n"         \
    "jc 1b\n"                                   \
    "cmpl $256, %1\n"                           \
    "je 3f\n"                                   \
    "2:" __LOCK_PREFIX "btr %%rax,%1\n"         \
    "jmp 0b\n"                                  \
    "3: cmpl $0, %2\n"                          \
    "jne 2b\n"                                  \
    __LOCK_PREFIX "incl %1\n"                   \
    __LOCK_PREFIX "btr\r %%rax,%1\n"            \
    :: "rI"(8), "m"(*(&(((rw_spinlock_t*)s)->__w))), \
    "m"( *(&(((rw_spinlock_t*)s)->__r) ) ) : "%rax", "memory" )


#define arch_rw_spinlock_unlock_read(s) \
    __asm__ __volatile__( __LOCK_PREFIX "decl %0\n"        \
                          :: "m"( *(&(((rw_spinlock_t*)s)->__r)) ) \
                          : "memory" ) \

#define arch_rw_spinlock_unlock_write(s) \
    __asm__ __volatile__( __LOCK_PREFIX "decl %0\n"        \
                          :: "m"( *(&(((rw_spinlock_t*)s)->__w)) ) \
                          : "memory" ) \


#endif
