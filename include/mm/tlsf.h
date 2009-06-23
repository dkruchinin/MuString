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
 * (c) Copyright 2008 Dan Kruchinin <dan.kruchinin@gmail.com>
 *
 * include/mm/tlsf.h: TLSF O(1) page allocator
 * Based on research of M. Masmano, I. Ripoll and A. Crespo
 *
 */

/**
 * @file include/mm/tlsf.h
 * @author Dan Kruchinin
 */

#ifndef __MSTRING_TLSF_H__
#define __MSTINRG_TLSF_H__

#include <config.h>
#include <ds/list.h>
#include <mstring/stddef.h>
#include <mm/page.h>
#include <mm/mmpool.h>
#include <mm/page_alloc.h>
#include <sync/spinlock.h>
#include <mstring/smp.h>
#include <mstring/types.h>

#define TLSF_FLD_SIZE       8  /**< TLSF first level directory size */
#define TLSF_SLD_SIZE       4  /**< TLSF second level directory size */
#define TLSF_CPUCACHE_PAGES 32 /*  */
#define TLSF_FIRST_OFFSET   4  /**< TLSF first offset */

#define TLSF_SLDS_MIN 2 /* Minimal number of SLDs in each FLD entry */
#define TLSF_FLDS_MAX 10 /* Maximum number of FLDs */
#define TLSF_FLD_BITMAP_SIZE TLSF_FLD_SIZE /* FLD bitmap size */
#define TLSF_SLD_BITMAP_SIZE round_up((TLSF_FLD_SIZE * TLSF_SLD_SIZE), 8) /* SLD bitmap size */

#ifdef CONFIG_SMP
typedef struct tlsf_percpu_cache {
  int noc_pages;     /**< Number of pages in cache */
  list_head_t pages; /**< List of available pages */
} tlsf_percpu_cache_t;
#endif /* CONFIG_SMP */

/**
 * @struct tlsf_node_t
 * TLSF FLD node(i.e. SLD)
 * containing information about each SLD node in
 * the FLD that owns given node.
 */
typedef struct tlsf_node {
  int num_blocks;     /**< Total number of page blocks in node */
  int max_avail_size; /**< Size of max available block */
  list_head_t blocks; /**< The list of all blocks in node */
} tlsf_node_t;

/**
 * @typedef uint8_t tlsf_bitmap_t
 * Standard TLSF bitmap.
 */
typedef uint8_t tlsf_bitmap_t;

/**
 * @struct tlsf_t
 * TLSF allocator structure containing
 * all TLSF-allocator-specific information.
 */
typedef struct tlsf {
  struct {
    tlsf_node_t nodes[TLSF_SLD_SIZE]; /**< FLD nodes(i.e. SLDs) */
    int total_blocks;                 /**< Total number of available blocks in all nodes */
  } map[TLSF_FLD_SIZE];               /**< TLSF map that contains FLDs */

#ifdef CONFIG_SMP
  tlsf_percpu_cache_t *percpu[CONFIG_NRCPUS];
#endif /* CONFIG_SMP */

  spinlock_t lock;
  mmpool_t *owner;                   /**< Type of pool that owns given TLSF allocator */
  uint8_t slds_bitmap[TLSF_SLD_BITMAP_SIZE];
  uint8_t fld_bitmap;  
} tlsf_t;

void tlsf_allocator_init(mmpool_t *pool);

#endif /* __MSTRING_TLSF_H__ */
