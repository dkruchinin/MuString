#ifndef __TYPES_H__
#define __TYPES_H__

#include <config.h>
#include <eza/arch/types.h>

#define NULL ((void *)0)
#define true  1
#define false 0

typedef uchar_t unsigned char;
typedef uint_t unsigned int;
typedef ulong_t unsigned long;
typedef uint_t bool;
typedef ulong size_t;

#ifdef CONFIG_ALWAYS_INLINE
#define always_inline inline __attribute__((always_inline))
#else
#define always_inline inline
#endif /* CONFIG_ALWAYS_INLINE */

#endif /* __TYPES_H__ */
