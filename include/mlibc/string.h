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
 * (c) Copyright 2008 Dan Kruchinin <dan,kruchinin@gmail.com>
 *
 * include/mlibc/string.h: contains main kernel types and prototypes for dealing
 *                         with strings.
 *
 */

#ifndef __STRING_H__
#define __STRING_H__ 

#include <mlibc/types.h>
#include <eza/arch/string.h>

#ifndef ARCH_MEMSET
static inline void *memset(void *s, int c, size_t n)
{
    if ((uintptr_t)s & 0x1) {
        register char *to = (char *)s;
        
        while (n-- > 0)
            *to++ = c;
    }
    else { /* address is aligned by 2 */
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
#else
#define memset(s, c, n) arch_memset(s, c, n)
#endif /* ARCH_MEMSET */


#ifndef ARCH_MEMCPY
static inline void *memcpy(void *dst, const void *src, size_t n)
{
    register char *to = (char *)dst;
    register const char *from = src;

    while(n-- > 0)
        *to++ = *from++;

    return dst;
}
#else
#define memcpy(dst, src, n) arch_memcpy(dst, src, n)
#endif /* ARCH_MEMCPY */


#ifndef ARCH_MEMCMP
static inline int memcmp(const void *s1, const void *s2, size_t n)
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
#else
#define memcmp(s1, s2, n) arch_memcmp(s1, s2, n)
#endif /* ARCH_MEMCMP */

#ifndef ARCH_STRLEN
static inline size_t strlen(const char *s)
{
    size_t n = 0;
    register const char *p = s;

    for (; *p != '\0'; p++, n++);
    return n;
}
#else
#define strlen(s) arch_strlen(s)
#endif /* ARCH_STRLEN */


#ifndef ARCH_STRNCPY
static inline char *strncpy(char *dst, const char *src, size_t n)
{
    register char *to = dst;
    register const char *from = src;
    
    while (n-- > 0)
        *to++ = *from++;

    *to = '\0';
    return dst;
}
#else
#define strncpy(dst, src, n) arch_strncpy(dst, src, n)
#endif /* ARCH_STRNCPY */

#ifndef ARCH_STRCPY
static inline char *strcpy(char *dst, const char *src)
{
    register char *to = dst;
    register const char *from = src;
    
    while (*from != '\0')
        *to++ = *from++;

    *to = '\0';
    return dst;
}
#else
#define strcpy(dst, src) arch_strcpy(dst, src)
#endif /* ARCH_STRCPY */


#ifndef ARCH_STRNCAT
static inline char *strncat(char *dst, const char *src, size_t n)
{
    register char *to = dst;
    register const char *from = src;

    for (; *to != '\0'; to++);
    while (n-- > 0)
        *to++ = *from++;

    *to = '\0';
    return dst;
}
#else
#define strncat(dst, src, n) arch_strncat(dst, src, n)
#endif /* ARCH_STRNCAT */


#ifndef ARCH_STRCAT
static inline char *strcat(char *dst, const char *src)
{
    register char *to = dst;
    register const char *from = src;

    for (; *to != '\0'; to++);
    while (*from != '\0')
        *to++ = *from++;

    *to = '\0';
    return dst;
}
#else
#define strcat(dst, src) arch_strcat(dst, src)
#endif /* ARCH_STRCAT */


#ifndef ARCH_STRNCMP
static inline int strncmp(const char *s1, const char *s2, size_t n)
{
    register const char *p1 = s1;
    register const char *p2 = s2;

    for (; n > 0; n--, p1++, p2++) {
        if (*p1 != *p2)
            return (*p1 - *p2);
    }

    return 0;
}
#else
#define strncmp(s1, s2, n) arch_strncmp(s1, s2, n)
#endif /* ARCH_STRNCMP */


#ifndef ARCH_STRCMP
static inline int strcmp(const char *s1, const char *s2)
{
  register const char *p1 = s1;
  register const char *p2 = s2;
  
  for (; (*p1 != '\0') && (*p2 != '\0'); p1++, p2++) {
    if (*p1 != *p2)
      return (*p1 - *p2);
  }
  if (*p1 == '\0')
    return -1;
  else if (*p2 == '\0')
    return 1;
  else
    return 0;
}
#else
#define strcmp(s1, s2) arch_strcmp(s1, s2)
#endif /* ARCH_STRCMP */


#ifndef ARCH_STRCHR
static inline char *strchr(const char *s, int c)
{
  register const char *p = s;

  for (; (*p != '\0') && (*p != c); p++);
  return (*p == '\0') ? NULL : (char *)(s + (p - s));
}
#else
#define strchr(s, c) arch_strchr(s, c)
#endif /* ARCH_STRCHR */


#ifndef ARCH_STRRCHR
static inline char *strrchr(const char *s, int c)
{
  register const char *p = s;

  for (; (*p != '\0') && (*p != c); p++);
  if (*p == '\0')
    return NULL;
  while (*p++ == c);

  return (char *)(s + (p - s));
}
#else
#define strrchr(s, c) arch_strrchr(s, c)
#endif /* ARCH_STRRCHR */


#ifndef ARCH_STRSTR
static inline char *strstr(const char *haystack, const char *needle)
{
  register const char *s1 = haystack;
  register const char *s2 = needle;

  for (; (*s1 != '\0') && (*s1 != *s2); s1++);
  while ((*s1 != '\0') && (*s1 != '\0') && (*s1++ == *s2++));
  if ((*s1 == '\0') || (*s2 == '\0'))
    return NULL;

  return (char *)(haystack + (s1 - haystack));
}
#else
#define strstr(haystack, needle) arch_strstr(haystack, needle)
#endif /* ARCH_STRSTR */

#endif /*__STRING_H__*/

