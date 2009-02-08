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
 * (c) Copyright 2008 Michael Tsymbalyuk <mtzaurus@gmail.com>
 *
 * include/eza/amd64/signal.h: AMD64-specific data types and prototypes
 *                             for kernel signal delivery subsystem.
 */

#ifndef __ARCH_SIGNAL_H__
#define  __ARCH_SIGNAL_H__

#include <eza/arch/types.h>
#include <eza/arch/bits.h>

#define signal_matches(m,s) arch_bit_test((m),(s))
#define sigdelset(m,s)  arch_bit_clear((m),(s))
#define sigaddset(m,s)  arch_bit_set((m),(s))

#endif
