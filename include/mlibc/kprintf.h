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
 * (c) Copyright 2006,2008 Dan Kruchinin <dan,kruchinin@gmail.com>
 *
 * include/mlibc/kprintf.h
 *
 */

#ifndef __KPRINTF_H__
#define __KPRINTF_H__

/* type of data, that will be printed (0 - 2 bits) */
#define FMT_TNUM    0x00001 /* number */
#define FMT_TSTR    0x00002 /* string */
#define FMT_TCHAR   0x00004 /* character */
/* flags */
#define FMT_FLJ     0x00008 /* '-' flag. it means, that output will be left-justigfird */
#define FMT_FZERO   0x00010 /* zero flag */
#define FMT_FSIG    0x00020 /* signed digit or just '+' flag */
#define FMT_FSPACE  0x00040 /* if first simbol of format sequince is not digit, print space. if + and space are enable, use + */
#define FMT_FPREF   0x00080 /* prefix before digit (x, X, o only) */
/* dot, just dot */
#define FMT_DOT     0x00100
/* length modifiers */
#define FMT_LCHAR   0x00200
#define FMT_LSHORT  0x00400
#define FMT_LLONG   0x00800
#define FMT_LDLONG  0x01000
#define FMT_LSIZET  0x02000
/* extension flags for digit type */
#define FMT_DOCT    0x04000 /* octal digit */
#define FMT_DDEC    0x08000 /* decimal digit */
#define FMT_DHEX    0x10000 /* hexadecimal digit */
#define FMT_DHEX_UP 0x20000 /* hexadecimal in upper case */
#define FMT_DUNSIG  0x40000 /* unsigned digit */

/* some format masks */
#define FMT_FMASK   0x00F8 /* mask for flags */
#define FMT_LMASK   0x3E00 /* mask for length modifiers */

#define FMT_DBUF_SIZE   64  /* length buffer for temprary digits holding */
#define KBQ_TMPBUF_SIZE 1024

/* kernel buffer getting stratagy */
#define KBUF_BYREF true  /* get string from kernel buffer by reference */
#define KBUF_COPY  false /* get string from kernel buffer by copying */

/* some flags for [v]kfprintf */
#define KO_NORMAL
#define KO_WARNING "[WARNING] "
#define KO_ERROR   "[ERROR] "
#define KO_PANIC   "[PANIC] "
#define KO_DEBUG   "[DEBUG] "

#define KBUF_SIZE 4096

#ifndef __ASM__
#include <mlibc/stdarg.h>
#include <eza/arch/types.h>

typedef struct kbuffer {  
  size_t cur_size;
  size_t size;
  char space[KBUF_SIZE];
} kbuffer_t;

/**
 * @fn void kprintf(const char *, ...)
 * @brief Kernel formatted print
 *
 * This function acts just like standard printf except it can't
 * handle and display floats and doubles. Output is printed
 * to default kernel console.
 *
 * @param fmt - printf-like format
 * @param ... - format arguments
 */
void kprintf(const char *fmt, ...);

void sprintf(char *str, const char *fmt, ...);
void snprintf(char *str, size_t size, const char *fmt, ...);

/**
 * @fn vkprintf(const char *, va_list);
 * @brief vkprintf is similar to vprintf from standard library
 *
 * vkprintf is very similar to vprintf from standard library,
 * except it can't handler floats and it outputs result string
 * to default kernel console.
 *
 * @param fmt - printf-like format
 * @param ap  - va_list containing format arguments
 */
void vkprintf(const char *fmt, va_list ap);

/**
 * @fn size_t vsprintf(char *, const char *, va_list);
 * @brief Formatted output to string buffer
 * @param dest - destination string buffer
 * @param fmt  - printf-like format
 * @param ap   - va_list with format arguments
 *
 * @return length of written data
 */
size_t vsprintf(char *dest, const char *fmt, va_list ap);

/**
 * @fn size_t vsnprintf(char *, const size_t, const char*, va_list);
 * @brief Formatted output to string buffer limited by length
 * @param dest  - destination string buffer
 * @param dsize - length limit
 * @param fmt   - printf-like format
 * @param ap    - va_list containing format arguments
 *
 * @return length of written data
 */
size_t vsnprintf(char *dest, const size_t dsize, const char *fmt, va_list ap);

/* functions defenition for kernel buffer */
size_t kbuf_insert(const char *, const size_t);
char *kbuf_get(void);

#ifdef CONFIG_DEBUG
#define kprintf_dbg(fmt, args...) kprintf(KO_DEBUG, fmt, ##args)
#else
#define kprintf_dbg(fmt, args...)
#endif /* CONFIG_DEBUG */

#endif /* __ASM__ */
#endif /* __KPRINTF_H__ */
