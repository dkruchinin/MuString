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
 * (c) Copyright 2008 Dan Kruchinin <dan,kruchinin@gmail.com>
 *
 */

#include <mstring/types.h>
#include <mstring/string.h>
#include <arch/string.h>

#ifndef ARCH_MEMSET
void *memset(void *s, int c, size_t n)
{
  if (1/*(uintptr_t)s & 0x1*/) {
    register char *to = (char *)s;
        
    while (n-- > 0)
      *to++ = c;
  }
  else { 
    register uint16_t *to = (uint16_t *)s;
    register uint16_t tmp = ((c & 0xff) | ((c & 0xff) << 8));
    size_t len = n >> 1;
      
    while (len-- > 0)
      *to++ = tmp;
    if (n & 0x1)            
      *(char *)to = c;
  }

  return s;
}
#endif /* !ARCH_MEMSET */

#ifndef ARCH_MEMCPY
void *memcpy(void *dst, const void *src, size_t n)
{
  register char *to = (char *)dst;
  register const char *from = src;
  
  while(n-- > 0) {
    *to++ = *from++;
  }

  return dst;
}
#endif /* !ARCH_MEMCPY */

#ifndef ARCH_MEMMOVE
#define MEMMOVE_BUFSIZE 256

void *memmove(void *dst, const void *src, size_t n)
{
  char buf[MEMMOVE_BUFSIZE];
  register size_t chunk = MEMMOVE_BUFSIZE;
  register size_t copied = 0;
  
  while (n) {
    if (unlikely(chunk > n)) {
      chunk = n;
    }

    memcpy(buf, src + copied, chunk);
    memcpy(dst + copied, buf, chunk);
    copied += chunk;
    n -= chunk;
  }

  return dst;
}
#endif /* !ARCH_MEMMOVE */

#ifndef ARCH_MEMCMP
int memcmp(const void *s1, const void *s2, size_t n)
{
  register const char *p1, *p2;

  p1 = (const char *)s1;
  p2 = (const char *)s2;
  for (; n > 0; p1++, p2++, n--) {
    if (*p1 != *p2)
      return (*p1 - *p2);
  }
  
  return 0;
}
#endif /* !ARCH_MEMCMP */
