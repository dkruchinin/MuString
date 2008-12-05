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
 * include/kernel.h: contains main kernel types and prototypes for common
 *                   kernel routines.
 *
 */

#ifndef __KERNEL_H__
#define __KERNEL_H__ 

#define LOGBUFFER_LEN  32768

#define _b2kb(b)    ((b) >> 10)
#define _kb2b(kb)   ((kb) << 10)
#define _kb2mb(kb)  ((kb) >> 10)
#define _mb2kb(mb)  ((mb) << 10)
#define _b2mb(b)    (_kb2mb(_b2kb(b)))
#define _mb2b(mb)   (_kb2b(_mb2kb(mb)))

#define likely(x)   __builtin_expect((x),1)
#define unlikely(x) __builtin_expect((x),0)
#define __unused__ __attribute__ ((unused))

void panic(const char *format, ...);
   
#endif /* __KERNEL_H__ */

