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
 * eza/amd64/strmem.c: mem*() functions arch depended
 *
 */

#include <eza/arch/types.h>
#include <eza/arch/memstr.h>

/* standart memcpy() */
inline void *__memcpy(void *dest,const void *src,size_t size)
{
  unative_t d0,d1,d2;

  __asm__ volatile("rep movsq\n\t"
		   "movq %4, %%rcx\n\t"
		   "andq $7, %%rcx\n\t"
		   "jz 1f\n\t"
		   "rep movsb\n\t"
		   "1:\n"
		   : "=&c" (d0), "=&D" (d1), "=&S" (d2)
		   : "0" ((unative_t)(size/8)), "g" ((unative_t)size),"1" ((unative_t)dest), "2" ((unative_t)src)
		   : "memory");

  return dest;
}

/* standart memcmp() */
inline int __memcmp(const void *dest,const void *src,size_t size)
{
  unative_t d0,d1,d2;
  unative_t ret;

  __asm__ volatile("repe cmpsb\n\t"
		   "je 1f\n\t"
		   "movq %3, %0\n\t"
		   "addq $1, %0\n\t"
		   "1:\n"
		   : "=a" (ret), "=%S" (d0), "=&D" (d1), "=&c" (d2)
		   : "0" (0), "1" (src), "2" (dest), "3" ((unative_t)size));

  return ret;
}

inline void __memsetb(uintptr_t dest,size_t size,uint8_t sym)
{
  unative_t d0,d1;

  __asm__ volatile ("rep stosb\n\t"
		    : "=&D" (d0), "=&c" (d1), "=a" (sym)
		    : "0" (dest), "1" ((unative_t)size), "2" (sym)
		    : "memory");

  return;
}

void *arch_memset(void *dest,uint8_t s,size_t size)
{
  __memsetb((uintptr_t)dest,size,s);

  return dest;
}

void *arch_memcpy(void *dest,const char *src,size_t size)
{
  return __memcpy(dest,src,size);
}

int arch_memcmp(const void *dest,const char *src,size_t size)
{
  return __memcmp(dest,src,size);
}

