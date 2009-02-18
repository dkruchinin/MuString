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
 * @brief TLSF O(1) page allocator
 * 
 * Main concept:
 * (Based on research of M. Masmano, I. Ripoll and A. Crespo)
 * TLSF page allocator has allocation and freeing time = O(1).
 * Allocator manipulates with FLD(first level directory) and SLD(second level directory). \
 *
 * FLD contains entries having size = power of two.
 * for example FLD entries may looks like the following:
 *   FLD:   [0]   |    [1]   |    [2]   |    [4]    |     [5]    
 * range: 0 .. 15 | 16 .. 31 | 32 .. 63 | 64 .. 127 | 128 .. 255
 *
 * SLD splits each range that creates FLD(i.e. first_power_of_2 .. next_pwer_of_2 - 1)
 * to N identical subranges. N may increase depending of FLD entrie's power of two.
 * For example for FLD map that was presented above, SLDs may looks like this(assuming
 * that N = 4):
 *                        SLD RANGES:
 * FLD[0];|off: 4
 *        + SLDs = (000 .. 003), (004 .. 007), (008 .. 011), (012 .. 015);
 * FLD[1];|off: 4  |          |  |          |  |          |  |          |
 *        + SLDs = (016 .. 019), (020 .. 023), (024 .. 027), (028 .. 031);
 * FLD[2];|off: 8  |          |  |          |  |          |  |          |
 *        + SLDs = (032 .. 029), (030 .. 037), (038 .. 045), (046 .. 063);
 * FLD[m];|off: x  |          |  |          |  |          |  |          |
 *        + SLDs = ( ........ ), ( ........ ), ( ........ ), ( ........ );
 * FLD[4];|off: 32 |          |  |          |  |          |  |          |
 *        + SLDs = (128 .. 159), (160 .. 191), (192 .. 223), (224 .. 255); 
 *
 * FLD and SLD contain corresponding bitmaps to simplify searching process.
 *
 * @author Dan Kruchinin
 */

#ifndef __TLSF_H__
#define __TLSF_H__

#include <config.h>
#include <ds/list.h>
#include <mlibc/stddef.h>
#include <mm/page.h>
#include <mm/mmpool.h>
#include <mm/pfalloc.h>
#include <eza/spinlock.h>
#include <mlibc/types.h>

#define TLSF_FLD_SIZE       5 /**< TLSF first level directory size */
#define TLSF_SLD_SIZE       4 /**< TLSF second level directory size */
#define TLSF_CPUCACHE_PAGES 8 /* TODO DK: implement TSLF percpu caches */
#define TLSF_FIRST_OFFSET   4 /**< TLSF first offset */


#define TLSF_SLDS_MIN 2 /* Minimal number of SLDs in each FLD entry */
#define TLSF_FLDS_MAX 8 /* Maximum number of FLDs */
#define TLSF_FLD_BITMAP_SIZE TLSF_FLD_SIZE /* FLD bitmap size */
#define TLSF_SLD_BITMAP_SIZE round_up((TLSF_FLD_SIZE * TLSF_SLD_SIZE), 8) /* SLD bitmap size */

/**
 * @struct tlsf_node_t
 * TLSF FLD node(i.e. SLD)
 * containing information about each SLD node in
 * the FLD that owns given node.
 */
typedef struct __tlsf_node {
  int blocks_no;      /**< Total number of page blocks in node */
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
typedef struct __tlsf {
  struct {
    tlsf_node_t nodes[TLSF_SLD_SIZE]; /**< FLD nodes(i.e. SLDs) */
    int total_blocks;                 /**< Total number of available blocks in all nodes */
  } map[TLSF_FLD_SIZE];               /**< TLSF map that contains FLDs */
  list_head_t *percpu_cache;          /* TODO DK: <-- */
  spinlock_t lock;
  mm_pool_t *owner;                   /**< Type of pool that owns given TLSF allocator */
  uint8_t slds_bitmap[TLSF_SLD_BITMAP_SIZE];
  uint8_t fld_bitmap;  
} tlsf_t;

/**
 * Initialize tlsf allocator.
 * @param pool - A pointer to pool allocator bins to.
 */
void tlsf_allocator_init(mm_pool_t *pool);

#ifdef CONFIG_DEBUG_MM
void tlsf_validate_dbg(void *_tlsf);
#else
#define tlsf_validate_dbg(_tlsf)
#endif /* CONFIG_MM_DEBUG */

#endif /* __TLSF_H__ */
