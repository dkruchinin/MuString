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
 * (c) Copyright 2008 Dan Kruchinin <dk@jarios.org>
 *
 * include/mm/slab.h: SLAB allocator
 *
 */

/**
 * @file include/mm/slab.h
 * @brief SLAB allocator API
 * @author Dan Kruchinin
 */

#ifndef __SLAB_H__
#define __SLAB_H__

#include <config.h>
#include <ds/list.h>
#include <mlibc/stddef.h>
#include <mm/page.h>
#include <mm/mmpool.h>
#include <eza/smp.h>
#include <eza/spinlock.h>
#include <eza/arch/atomic.h>
#include <mlibc/types.h>

#define MEMCACHE_EMPTYSLABS_MAX  (CONFIG_NRCPUS)

/* Quantity of pages per slab for generic caches */
#define GENERIC_SLAB_PAGES   1

/* Minimal allowed object size */
#define FIRST_GENSLABS_POW2  BYTES_LONG_SHIFT
#define LAST_GENSLABS_POW2   10

/* min allowed object size */
#define SLAB_OBJECT_MIN_SIZE (1 << FIRST_GENSLABS_POW2)
/* Max allowed object size */
#define SLAB_OBJECT_MAX_SIZE (1 << LAST_GENSLABS_POW2)

/* number of generic memory caches(except memory caches for memcache_t and slab_t) */
#define SLAB_GENERIC_CACHES  (LAST_GENSLABS_POW2 - FIRST_GENSLABS_POW2 + 1)

#ifdef CONFIG_DEBUG_SLAB_MARK_PAGES
#define SLAB_PAGE_MARK_SIZE  (sizeof(unsigned int))
/* Magic base of memory cache identifier holding in each slab's page */
#define SLAB_PAGE_MARK_BASE    (0xCAFDAF0)
/* Very first memory cache type (last type is ((1 << 31) - SLAB_MAGIC_BASE) */
#define SLAB_FIRST_VERSION     1
#endif /* CONFIG_DEBUG_SLAB_PAGE_GUARD */

#ifdef CONFIG_DEBUG_SLAB_OBJGUARDS
/* Min space required for holding debug information in slab's free objects */
#define SLAB_OBJDEBUG_MINSIZE ((sizeof(int) * 2) + sizeof(uintptr_t))
#define SLAB_LEFTGUARD_OFFS   (sizeof(int))
#define SLAB_RIGHTGUARD_OFFS  (sizeof(uintptr_t) + sizeof(int))
/* Magic number that is written before address of next item in objects list */
#define SLAB_OBJLEFT_GUARD    (0xABCD)
/* Magic number that is written after address of next item in objects list */
#define SLAB_OBJRIGHT_GUARD   (0xDCBA)
#endif /* CONFIG_DEBUG_SLAB_OBJGUARDS */

#ifdef CONFIG_DEBUG_SLAB
/* Max length of memory cache debug name */
#define MEMCACHE_DBG_NAME_MAX 128
#endif /* CONFIG_DEBUG_SLAB */


/**
 * @enum slab_state_t
 * @brief Describe all possible states slab may have.
 */
typedef enum __slab_state {
  SLAB_EMPTY = 1, /**< Slab is absolutely empty */
  SLAB_PARTIAL,   /**< Some objects were allocated from slab */
  SLAB_FULL,      /**< Slab is completely full */
  SLAB_ACTIVE,    /**< Slab is active. */
} slab_state_t;


typedef struct __memcache memcache_t;

/**
 * @struct slab_t
 * @brief Slab structure
 *
 * Slab is an abstract unit that owns one or more
 * continous pages which are splitted with chunks of similar size.
 * Slab may be in four states: empty, partial, full and active.
 *
 * @see slab_state_t
 * @see memcache_t
 */
typedef struct __slab {
  void  *objects;       /**< A list of free objects */
  page_frame_t *pages;  /**< Pages slab owns */
  memcache_t *memcache; /**< Parent memory cache */
  int nobjects;         /**< Number of free objects in slab */
  slab_state_t state;   /**< Slab state */
} slab_t;

typedef uint16_t memcache_flags_t;

/**
 * The following flags controls memory cache behaviour
 * @see memcache_t
 */
#define SMCF_UNIQUE     (1 << MMPOOLS_SHIFT)
#define SMCF_IMMORTAL   (1 << (MMPOOLS_SHIFT + 1))
#define SMCF_LAZY       (1 << (MMPOOLS_SHIFT + 2))
#define __SMCF_BIT_LOCK (1 << (MMPOOLS_SHIFT + 3))

#define SMCF_MASK (MMPOOLS_MASK | SMCF_UNIQUE | SMCF_IMMORTAL | SMCF_LAZY)

/* slab alloc flags */
#define SAF_ATOMIC     0x01
#define SAF_DONT_GROW  0x02
#define SAF_MEMNULL    0x04

/* generic slabs default behaviour control flags */

/**
 * @struct memcache_t
 * @brief Memory cache general structure
 *
 * Memory cache is an abstract unit owning some quantity of
 * slabs. Memory cache is some kind of set containing control
 * and behaviour information for slabs.
 *
 * @see slab_t
 * @see memcache_flags_t
 */
struct __memcache {
  list_head_t avail_slabs;               /**< List of empty and partial slabs */
  list_head_t full_slabs;
  list_node_t memcache_node;

  slab_t *active_slabs[CONFIG_NRCPUS];   /**< Active slabs(there may be only one active slab if SMCF_SHARED was set) */
  int object_size;
  int pages_per_slab;
  int usecount;

  struct {
    int nslabs;
    int nempty_slabs;
    int npartial_slabs;
  } stat;

  memcache_flags_t flags;
  
#ifdef CONFIG_DEBUG_SLAB
  char name[MEMCACHE_DBG_NAME_MAX];
  int mark_version;
#endif /* CONFIG_DEBUG_SLAB */
};

void slab_allocator_init(void);

/**
  * @brief Create new memory cache
 * @param name  - Name of new memory cache
 * @param size  - Object size
 * @param pages - Number of pages per each cache's slab
 * @param flags - Behaviour control flags
 *
 * @return New memory cache on success, NULL on error(if there is no enough memory to allocate cache)
 * @see memcache_t
 * @see memcache_flags
 */
memcache_t *create_memcache(const char *name, size_t size,
                            int pages, memcache_flags_t flags);

/**
 * @brief Allocate object from memory cache @a cache
 *
 * @param cache - A pointer to memory cache object will be allocated from
 * @return A pointer to allocated object on success, NULL if no more memory available
 * @see memcache_t
 */
void *alloc_from_memcache(memcache_t *cache, int alloc_flags);

/**
 * @brief Allocate @a size bytes from generic memory caches.
 *
 * memalloc finds first size that is greater than or equals to
 * @a size and allocates it. (Note, typically generic caches have sizes = 2^k).
 *
 * @param size - Number of bytes to allocate
 * @return A pointer to allocated memory on success, NULL if no more memory available.
 */
void *memalloc(size_t size);

/**
 * @brief Free @a mem to its slab.
 * @note: @a mem must be valid allocated from slab object.
 */
void memfree(void *mem);

#ifdef CONFIG_DEBUG_SLAB
void slab_verbose_enable(void);
void slab_verbose_disable(void);
#endif /* CONFIG_DEBUG_SLAB */

#endif /* __SLAB_H__ */
