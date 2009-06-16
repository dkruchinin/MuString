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
 * include/mstring/amd64/smp.h: SMP support on amd64 architecture.
 *
 */

#ifndef __MSTRING_ARCH_SMP_H__
#define __MSTRING_ARCH_SMP_H__

#include <arch/seg.h>
#include <mstring/types.h>

extern int ap_boot_start, ap_boot_end,
    kernel_jump_addr, ap_jmp_rip;

extern void ap_boot(void);
extern void smp_start32(void);
extern void main_smpap_routine(void);
void arch_smp_init(int ncpus);

void smp_local_timer_interrupt_handler(void);
void smp_scheduler_interrupt_handler(void);

#endif /* __MSTRING_ARCH_SMP_H__ */

