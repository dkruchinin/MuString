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
 * (c) Copyright 2008 Dan Kruchinin <dan.kruchinin@gmail.com>
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
#include <eza/smp.h>
#include <eza/spinlock.h>
#include <eza/arch/atomic.h>
#include <eza/arch/types.h>

/* Max number of empty slabs one memory cache may have. */
#define SLAB_EMPTYSLABS_MAX  (CONFIG_NRCPUS)
/* End address of objects list */
#define SLAB_OBJLIST_END     ((char *)0xF00BAAF)
/* Quantity of pages per slab for generic caches */
#define GENERIC_SLAB_PAGES   1
/* Minimal allowed object size */
#define SLAB_OBJECT_MIN_SIZE 8 
/* Max allowed object size */
#define SLAB_OBJECT_MAX_SIZE 512
#define FIRST_GENSLABS_POW2  3
#define LAST_GENSLABS_POW2   9
/* number of generic memory caches(except memory caches for memcache_t and slab_t) */
#define SLAB_GENERIC_CACHES  (LAST_GENSLABS_POW2 - FIRST_GENSLABS_POW2 + 1)

#ifdef DEBUG_SLAB
/* Min space required for holding debug information in slab's free objects */
#define SLAB_OBJDEBUG_MINSIZE ((sizeof(int) << 1) + sizeof(char *))
#define SLAB_LEFTGUARD_OFFS   sizeof(int)
#define SLAB_RIGHTGUARD_OFFS  (sizeof(char *) + sizeof(int))
/* Magic number that is written before address of next item in objects list */
#define SLAB_OBJLEFT_GUARD    (0xABCD)
/* Magic number that is written after address of next item in objects list */
#define SLAB_OBJRIGHT_GUARD   (0xDCBA)
/* Offset in each slab pages that is needed for holding debug information */
#define SLAB_PAGE_OFFS        (sizeof(unsigned int))
/* Magic base of memory cache identifier holding in each slab's page */
#define SLAB_MAGIC_BASE       (0xCAFDAF0)
/* Very first memory cache type (last type is ((1 << 31) - SLAB_MAGIC_BASE) */
#define SLAB_FIRST_TYPE       (1)

/*
 * Private memory cache strucutre containing
 * various debug information.
 */
struct memcache_debug_info {
  char *name;        /* Memory cache name */
  unsigned int type; /* Memory cache type */
  list_node_t n;     /* obvious */
};
#else
#define SLAB_PAGE_OFFS      0
#define SLAB_LEFTGUARD_OFFS 0
#define SLAB_RIGHGUARD_OFFS 0
#endif /* DEBUG_SLAB */


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
  char **objects;       /**< A list of free objects */
  page_frame_t *pages;  /**< Pages slab owns */
  memcache_t *memcache; /**< Parent memory cache */
  int nobjects;         /**< Number of free objects in slab */
  slab_state_t state;   /**< Slab state */
} slab_t;

/**
 * The following flags controls memory cache behaviour
 * @see memcache_t
 */
#define SMCF_PDMA      0x01 /**< Allocate pages for slabs from DMA pool */
#define SMCF_PGEN      0x02 /**< Allocate pages for slabs from GENERAL pool */
#define SMCF_SHARED    0x04 /**< Do not create percpu slabs */
#define SMCF_POISON    0x08 /**< Make slab objects "poison" after their freeing */
#define SMCF_CONST     0x10 /**< Do not create new slabs for memory cache */
#define SMCF_GENERIC   0x20 /**< Memory cache is generic(it can't bee destroyed) */
#define SMCF_MERGE     0x40 /**< Try to merge memory cache with existing one that has identical object size */
/* TODO DK: implement the following policies: SMCF_SHARED, SMCF_POISON, SMCF_MERGE */

/* generic slabs default behaviour control flags */
#define SLAB_GENERIC_FLAGS (SMCF_PDMA | SMCF_GENERIC)

/**
 * @typedef uint8_t memcache_flags_t
 * @brief Flags that control memory cache behaviour
 *
 * @see memcache_t
 * @see SMCF_PDMA
 * @see SMCF_PGEN
 * @see SMCF_SHARED
 * @see SMCF_POISON
 * @see SMCF_CONST
 * @see SMCF_MERGE
 */
typedef uint8_t memcache_flags_t;

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
  list_head_t inuse_slabs;               /**< List of active and full slabs */
  list_head_t available_slabs;           /**< List of empty and partial slabs */
  slab_t *active_slabs[CONFIG_NRCPUS];   /**< Active slabs(there may be only one active slab if SMCF_SHARED was set) */
#ifdef DEBUG_SLAB
  struct memcache_debug_info dbg; 
#endif /* DEBUG_SLAB */
  atomic_t nslabs;                 /**< Total number of slabs in cache */
  atomic_t nempty_slabs;           /**< Number of empty slabs in cache */
  atomic_t npartial_slabs;         /**< Number of partial slabs in cache */
  size_t object_size;              /**< Size of object which is allocated from slabs */
  spinlock_t lock;                 /**< Lock for cache's lists */
  int pages_per_slab;              /**< Number of pages per each slab created by this cache */
  memcache_flags_t flags;          /**< Behaviour control flags */
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
void *alloc_from_memcache(memcache_t *cache);

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

#ifdef DEBUG_SLAB
void slab_verbose_enable(void);
void slab_verbose_disable(void);
#endif /* DEBUG_SLAB */

#endif /* __SLAB_H__ */
