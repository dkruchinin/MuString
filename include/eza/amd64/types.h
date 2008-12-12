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
 * include/eza/amd64/types.h: types definions
 *
 */

#ifndef __ARCH_TYPES_H__
#define __ARCH_TYPES_H__

/* Some macro to make life a bit easier. */
#define KB(x) ((x)*1024)
#define MB(x) ((x)*1024*1024)

/* simple typedefs */
typedef signed char int8_t;
typedef signed short int16_t;
typedef signed int int32_t;
typedef signed long long int64_t;
typedef unsigned char uint8_t; /* unsigned */
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;
/* system used types */
typedef unsigned long uintptr_t;
typedef uint32_t pid_t;
typedef uint16_t uid_t;
typedef uint64_t tid_t;
typedef uint32_t usec_t;
typedef int32_t status_t;
typedef uint32_t shm_id_t;

/* abstraction typedef */
typedef uint64_t unative_t;
typedef uint64_t native_t;
typedef uint64_t ipl_t;

typedef unsigned long lock_t;

/* bit-related types. */
typedef uint32_t bit_idx_t;

/* SMP-related stuff. */
typedef uint32_t cpu_id_t;

typedef enum __task_privilege {
  TPL_KERNEL = 0,  /* Kernel task - the most serious level. */
  TPL_USER = 1,    /* User task - the least serious level */
} task_privelege_t;

#define TYPE_LONG_SHIFT  6
#define BITS_PER_LONG  64

#endif

