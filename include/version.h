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
 * (c) Copyright 2008 MadTirra <madtirra@jarios.org>
 *
 * kernel/version.c: Version related simple functions and definions
 *
 */

#ifndef __VERSION_H__
#define __VERSION_H__

#include <eza/arch/types.h>

#define KERNEL_VERSION     0
#define KERNEL_SUBVERSION  0
#define KERNEL_RELEASE     1

#define KERNEL_RELEASE_NAME  "Quoppa"


void print_kernel_version_info(void);
void swks_add_version_info(void);

#endif /* __VERSION_H__ */

