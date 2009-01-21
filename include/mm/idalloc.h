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
 * include/mm/idalloc.h: Init-data memory allocator
 *
 */

/**
 * @file include/mm/idalloc.h
 * @brief Init-data memory allocator
 *
 * Init-data memory allocator is used for dynamic
 * allocation of continous chunks of memory leaster than PAGE_SIZE
 * that are used for init-data. Init-data allocator can't free allocated blocks,
 * so it may be beneficial if you need to allocate some amount of memory leaster than
 * PAGE_SIZE *before* slabs are initialized or if you're sure allocated data'll never be fried.
 *
 * When idalloc is disabled it looks if it has any unused pages. If so, it returns them to
 * the pool's page frames allocator.
 *
 * @author Dan Kruchinin
 */

#ifndef __IDALLOC_H__
#define __IDALLOC_H__

#include <config.h>
#include <mm/mmpool.h>
#include <mm/page.h>
#include <eza/spinlock.h>
#include <eza/arch/types.h>

/**
 * @struct idalloc_memingo_t
 * Contains idalloc internal information.
 */
typedef struct __idalloc_meminfo {
  char *mem;                /**< Address starting from which data may be allocated */
  list_head_t avail_pages;  /**< A list of available pages */
  list_head_t used_pages;   /**< A list of already used(full) pages */
  spinlock_t lock;          /**< Obvious */
  int npages;               /**< Total number of pages idalloc owns */
  bool is_enabled;          /**< True if idalloc is enabled and false otherwise */
} idalloc_meminfo_t;

typedef __idalloc_vspaceinfo {
  ulong_t num_vpages;       /**< Size of virtual area pool in pages */
  ulong_t avail_vpages;     /**< Amount of available pages in virtual area pool */
  uintptr_t virt_top;       /**< First available address of virtual area pool */
  spinlock_t lock;
  bool is_enabled;          /**< Indicates if virtual space allocator is enabled */  
} idalloc_vspaceinfo_t;

extern idalloc_meminfo_t idalloc_meminfo;      /**< Global structure containing all idalloc informaion */
extern idalloc_vspaceinfo_t idalloc_vspaceinfo /**< Global structure containing all information about available virtual space */

/**
 * @brief Enable init-data allocator
 * CONFIG_IDALLOC_PAGES will be cutted from @a pool. (if available)
 *
 * @param pool - Memory pool idalloc may cut pages from.
 * @see mm_pool_t
 */
void idalloc_enable(mm_pool_t *pool);
void idalloc_vspace_enable(uintptr_t start_addr, ulong_t nvpages);
void idalloc_disable(void); /* TODO DK: redisign */

/**
 * @brief Allocate @a size memory chunk if possible
 *
 * @param size - A size of memory chunk to allocate.
 * @return A pointer to memory chunk of size @a size on success or NULL on failure.
 */
void *idalloc(size_t size);
void *idalloc_vregion(page_idx_t pages);

/**
 * @brief Check if idalloc is enabled
 * @return boolean
 */
static inline bool idalloc_is_enabled(void)
{
  return idalloc_meminfo.is_enabled;
}

#endif /* __IDALLOC_H__ */
