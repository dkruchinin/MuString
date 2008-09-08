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
 * include/eza/amd64/memstr.h: mem*() functions arch depended
 *
 */

#ifndef __MEMSTR_H__
#define __MEMSTR_H__

#include <eza/arch/types.h>

/* standart memcpy() */
extern inline void *__memcpy(void *dest,const void *src,size_t size);

/* standart memcmp() */
extern inline int __memcmp(const void *dest,const void *src,size_t size);

extern inline void __memsetb(uintptr_t dest,size_t size,uint8_t sym);

//#define ARCH_MEMSET  1
//#define ARCH_MEMCPY  1
//#define ARCH_MEMCMP  1

extern void *arch_memset(void *dest,uint8_t s,size_t size);
extern void *arch_memcpy(void *dest,const char *src,size_t size);
extern int arch_memcmp(const void *dest,const char *src,size_t size);

#endif /* __MEMSTR_H__ */
