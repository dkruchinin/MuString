#ifndef __TYPES_H__
#define __TYPES_H__

#include <config.h>
#include <eza/arch/types.h>

#define NULL ((void *)0)
#define true  1
#define false 0

typedef unsigned char uchar_t;
typedef unsigned int uint_t;
typedef unsigned long ulong_t;
typedef uint_t bool;
typedef ulong_t size_t;
typedef uint16_t uid_t;
typedef uint16_t gid_t;
typedef uint64_t mode_t;

#ifdef CONFIG_ALWAYS_INLINE
#define always_inline inline __attribute__((always_inline))
#else
#define always_inline inline
#endif /* CONFIG_ALWAYS_INLINE */

#endif /* __TYPES_H__ */
