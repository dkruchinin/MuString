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
 */

#include <arch/context.h>

.code64
.text

.globl kthread_fork_path
kthread_fork_path:
        sti
        RESTORE_MM
        RESTORE_GPRS
        popq %rax
        iretq

.globl user_fork_path_test
user_fork_path_test:
    xorq %rax,%rax

.globl user_fork_path
user_fork_path:
    xorq %rax,%rax
resume_userspace:
    RESTORE_ALL
1:      jmp 1b

