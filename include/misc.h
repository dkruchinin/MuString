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
 * include/misc.h: misc macros and functions
 *
 */

#ifndef __MISC_H__
#define __MISC_H__

#include <eza/arch/types.h>
#include <eza/arch/page.h>

#define isdigit(s)  (((s)>='0') && ((s)<='9'))
#define islower(s)  (((s)>='a') && ((s)<='z'))
#define isupper(s)  (((s)>='A') && ((s)<='Z'))
#define isalpha(s)  (is_lower(s) || is_upper(s))
#define isspace(s)  (((c)==' ') || ((c)=='\t') || ((c)=='\r') || ((c)=='\t'))

#define min(a,b)  ((a)<(b) ? (a) : (b))
#define max(a,b)  ((a)>(b) ? (a) : (b))

#define STRING(arg)      STRING_ARG(arg)
#define STRING_ARG(arg)  #arg
#define SIZE2KB(size)    (size >> 10)
#define SIZE2MB(size)    (size >> 20)

#define faddr(p)  ((uintptr_t)(p))

#define phys_overlap(w0,sw0,w1,sw1)  overlap(k2p(w0),sw0,k2p(w1),sw1)

/* calc overlap areas */
static inline int overlap(uintptr_t si1,size_t is1,uintptr_t si2,size_t is2)
{
  uintptr_t end1=si1+is1;
  uintptr_t end2=si2+is2;

  return (si1 < end2) && (si2 < end1);
}

#endif /* __MISC_H__ */

