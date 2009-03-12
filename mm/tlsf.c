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
 *
 */

/**
 * @file include/mm/tlsf.c
 * @brief TLSF O(1) page allocator
 * @author Dan Kruchinin
 */

#include <config.h>
#include <ds/list.h>
#include <mlibc/string.h>
#include <mlibc/assert.h>
#include <mlibc/stddef.h>
#include <mlibc/bitwise.h>
#include <mlibc/types.h>
#include <mm/page.h>
#include <mm/mmpool.h>
#include <mm/pfalloc.h>
#include <mm/idalloc.h>
#include <mm/tlsf.h>
#include <eza/spinlock.h>
#include <eza/errno.h>
#include <eza/arch/atomic.h>

/*
 * Main concept:
 * (Based on research of M. Masmano, I. Ripoll and A. Crespo)
 * TLSF page allocator has allocation and freeing time = O(1).
 * Allocator manipulates with FLD(first level directory) and SLD(second level directory). \
 *
 * FLD contains entries of power of two sizes.
 * for example FLD entries may looks like the following:
 *   FLD:   [0]   |    [1]   |    [2]   |    [4]    |     [5]
 * range: 0 .. 15 | 16 .. 31 | 32 .. 63 | 64 .. 127 | 128 .. 255
 *
 * SLD splits each FLD index range that creates FLD(i.e. first_power_of_2 .. next_pwer_of_2 - 1)
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
 */


#ifdef _LP64
typedef uint32_t tlsf_uint_t;
#else
typedef uint16_t tlsf_uint_t;
#endif /* _LP64 */

/*
 * information TLSF holds in the _private
 * field of page_frame structure.
 * (note: size of _private field is 32 bits)
 */
union tlsf_priv {
  ulong_t pad;
  struct {
    tlsf_uint_t flags; /* TLSF-specific flags */
    tlsf_uint_t size;  /* size of pages block */
  };
};

/* TLSF main indices */
struct tlsf_idxs {
  int fldi; /* FLD index */
  int sldi; /* SLD index */
};

enum {
  TLSF_PB_HEAD = 0,  /* Determines that a page is a block head */
  TLSF_PB_TAIL,      /* Determines that a page is a block tail */
  TLSF_PB_BUSY,
};

#define TLSF_PB_MASK ((1 << TLSF_PB_HEAD) | (1 << TLSF_PB_TAIL))

/* First available power of two */
static const int FLD_FPOW2 = 0;

/* Very last power of two */
static const int FLD_LPOW2 = TLSF_FLD_SIZE + TLSF_FIRST_OFFSET - 2;
static const int MAX_BLOCK_SIZE = (1 << (TLSF_FLD_SIZE + TLSF_FIRST_OFFSET - 1));

static inline void get_tlsf_ids(tlsf_uint_t size, struct tlsf_idxs *ids);
static inline tlsf_uint_t size_from_tlsf_ids(struct tlsf_idxs *ids);

/* Get number of SLDs in given FLD index */
#define __fld_offset(fldi)                                  \
  ((fldi) ? (TLSF_SLD_SIZE << ((fldi) - 1)) : TLSF_SLD_SIZE)
#define __fldi(size)                            \
  (__power2fldi(bit_find_msf(size)))
#define __sldi(size, fldi)                                      \
  (((size) & ~(1 << __fldi2power(fldi)) /  __fld_offset(fldi))

/* Convert FLD index to corresponding power of 2 */
#define __fldi2power(fldi)                      \
  ((fldi) ? ((fldi) + TLSF_FIRST_OFFSET - 1) : 0)

/* Convert power of two to corresponding FLD index */
#define __power2fldi(pow)                                               \
  (((pow) >= TLSF_FIRST_OFFSET) ? ((pow) - TLSF_FIRST_OFFSET + 1) : 0)

/* Get a chunk of SLDs bitmap corresponding to given fldi and sldi */
#define __sld_bitmap(tlsf, fldi)                                    \
  (*(tlsf_bitmap_t *)((tlsf)->slds_bitmap + (fldi) * TLSF_SLD_SIZE))

#define __fld_mark_avail(tlsf, fldi)            \
  (bit_set(&(tlsf)->fld_bitmap, fldi))
#define __fld_mark_unavail(tlsf, fldi)          \
  (bit_clear(&(tlsf)->fld_bitmap, fldi))
#define __fld_is_avail(tlsf, fldi)              \
  (bit_test(&(tlsf)->fld_bitmap, fldi))
#define __sld_mark_avail(tlsf, fldi, sldi)                          \
  (bit_set(&(tlsf)->slds_bitmap[(fldi) * TLSF_SLD_SIZE], sldi))
#define __sld_mark_unavail(tlsf, fldi, sldi)                        \
  (bit_clear(&(tlsf)->slds_bitmap[(fldi) * TLSF_SLD_SIZE], sldi))
#define __sld_is_avail(tlsf, fldi, sldi)                            \
  (bit_test(&(tlsf)->slds_bitmap[(fldi) * TLSF_SLD_SIZE], sldi))

/* get block size. */
static inline tlsf_uint_t pages_block_size_get(page_frame_t *block)
{
  union tlsf_priv *priv = (union tlsf_priv *)&block->_private;
  return priv->size;
}

static inline void pages_block_size_set(page_frame_t *block, tlsf_uint_t size)
{
  union tlsf_priv *priv = (union tlsf_priv *)&block->_private;
  priv->size = size;
}

static inline tlsf_uint_t pages_block_flags(page_frame_t *block)
{
  union tlsf_priv priv = { block->_private };
  return priv.flags;
}

static inline void pages_block_flags_set(page_frame_t *block, tlsf_uint_t flags)
{
  union tlsf_priv priv = { block->_private };

  bits_or(&priv.flags, flags);
  block->_private = priv.pad;
}

static inline void __block_flags_set_mask(page_frame_t *block, tlsf_uint_t mask)
{
  union tlsf_priv priv = { block->_private };

  bits_and(&priv.flags, mask);
  block->_private = priv.pad;
}

/* Find next available FLD index after "start" index */
static inline int __find_next_fldi(tlsf_t *tlsf, int start)
{
  return bit_find_lsf(tlsf->fld_bitmap & ~((1 << (start + 1)) - 1));
}

/* Find next available SLD index after "sldi_start" index */
static inline int __find_next_sldi(tlsf_t *tlsf, int fldi, int sldi_start)
{
  return
    bit_find_lsf(__sld_bitmap(tlsf, fldi) & ~((1 << (sldi_start + 1)) - 1));
}

/* Get TLSF FLD index and SLD index by block size */
static void get_tlsf_ids(tlsf_uint_t size, struct tlsf_idxs *ids)
{
  int pow2 = bit_find_msf(size);

  ids->fldi = __power2fldi(pow2);
  ids->sldi = (size & ~(1 << pow2)) / __fld_offset(ids->fldi);
}

/* Get block size from its FLD and SLD indices */
static inline tlsf_uint_t size_from_tlsf_ids(struct tlsf_idxs *ids)
{
  tlsf_uint_t size = 1 << __fldi2power(ids->fldi);
  size += ids->sldi * __fld_offset(ids->fldi);
  return size;
}

static void pages_block_create(page_frame_t *head, page_idx_t offs)
{
  page_frame_t *tail = head + offs;

  bit_set(&head->_private, TLSF_PB_HEAD);
  bit_set(&tail->_private, TLSF_PB_TAIL);
  pages_block_size_set(head, offs + 1);
  if (head != tail)
    pages_block_size_set(tail, offs + 1);
}

/* Deinitialize TLSF pages block: clear all internal bits and unset the size */
static void pages_block_destroy(page_frame_t *block_head)
{
  page_frame_t *block_tail = block_head + pages_block_size_get(block_head) - 1;

  ASSERT(bit_test(&block_head->_private, TLSF_PB_HEAD));
  ASSERT(bit_test(&block_tail->_private, TLSF_PB_TAIL));
  bit_clear(&block_head->_private, TLSF_PB_HEAD);
  bit_clear(&block_tail->_private, TLSF_PB_TAIL);
  pages_block_size_set(block_head, 0);
  if (block_head != block_tail)
    pages_block_size_set(block_tail, 0);
}

static void pages_block_insert(tlsf_t *tlsf, page_frame_t *block_head)
{
  struct tlsf_idxs ids;
  tlsf_node_t *sld_node;
  tlsf_uint_t size = pages_block_size_get(block_head);

  get_tlsf_ids(size, &ids);
  sld_node = tlsf->map[ids.fldi].nodes + ids.sldi;
  /* The greatest block should be the very last block in blocks list */
  if (size > sld_node->max_avail_size) {
    list_add2tail(&sld_node->blocks, &block_head->node);
    sld_node->max_avail_size = size;
  }
  else
    list_add2head(&sld_node->blocks, &block_head->node);

  /* Update bitmaps if the new block became the first available one in given SLD */
  if (sld_node->blocks_no++ == 0) {
    __sld_mark_avail(tlsf, ids.fldi, ids.sldi);

    /* Also it might be the first one in the corresponding FLD */
    if (tlsf->map[ids.fldi].total_blocks++ == 0)
      __fld_mark_avail(tlsf, ids.fldi);
  }
}

static void pages_block_remove(tlsf_t *tlsf, page_frame_t *block_head)
{
  struct tlsf_idxs ids;
  tlsf_node_t *sld_node;
  tlsf_uint_t size = pages_block_size_get(block_head);

  get_tlsf_ids(size, &ids);
  sld_node = tlsf->map[ids.fldi].nodes + ids.sldi;
  list_del(&block_head->node);
  if (--sld_node->blocks_no == 0) {
    /*
     * We've just removed the last available pages block with given FLD and SLD indices from the
     * TLSF blocks set. So, it's a time to mark corresponding FLD and SLD indices unavailable.
     */
    __sld_mark_unavail(tlsf, ids.fldi, ids.sldi);
    sld_node->max_avail_size = 0;
    if (--tlsf->map[ids.fldi].total_blocks == 0) {
      /* Unfortunatelly it was the last available SLD in given FLD index */
      __fld_mark_unavail(tlsf, ids.fldi);
    }
  }
  else {
    /* Update max available in the SLD node block size if necessary */
    if (sld_node->max_avail_size == pages_block_size_get(block_head)) {
      sld_node->max_avail_size =
        pages_block_size_get(list_entry(list_node_last(&sld_node->blocks), page_frame_t, node));
    }
  }
}

/*
 * Get the left neighbour of the block starting from "block_head" frame.
 * Left neighbour block should have its tail page strictly by one position
 * before the head of a given block. If the neighbour doesn't exitst, return NULL.
 */
static inline page_frame_t *__left_neighbour(page_frame_t *block_head)
{
  page_frame_t *page;

  page = pframe_by_number(pframe_number(block_head) - 1);
  if (!bit_test(&page->_private, TLSF_PB_TAIL) ||
      (page->pool_type != block_head->pool_type) || (page->flags & PF_RESERVED))
    return NULL;

  page -= pages_block_size_get(page) - 1;
  ASSERT(bit_test(&page->_private, TLSF_PB_HEAD));
  return page;
}

/*
 * Get the right neighbour of the block starting from "block_head" frame.
 * Right neigbour block should have its head page stryctly by one position
 * ofter the tail frame of given block. Return NULL if the neighbour doesn't exist.
 */
static inline page_frame_t *__right_neighbour(page_frame_t *block_head)
{
  page_frame_t *page;

  page = block_head + pages_block_size_get(block_head);
  if (!bit_test(&page->_private, TLSF_PB_HEAD) ||
      ((page->pool_type == block_head->pool_type)) || (page->flags & PF_RESERVED))
    return NULL;

  return page;
}

/*
 * Split block "block_root" into two new blocks. The head of block with size
 * (__block_size(block_root) - split_size) will be equal to current head of block_root
 * The head of new block that is cutted from original block root will have size
 * split_size and its root page is a page after tail page of new block_root.
 */
static page_frame_t *pages_block_split(tlsf_t *tlsf, page_frame_t *block_head, tlsf_uint_t split_size)
{
  tlsf_uint_t offset = pages_block_size_get(block_head) - split_size;
  page_frame_t *new_block;

  pages_block_destroy(block_head);
  new_block = pframe_by_number(pframe_number(block_head) + offset);
  pages_block_create(block_head, offset - 1);
  pages_block_create(new_block, split_size - 1);
  return new_block;
}

#define try_merge_left(tls, block_root)  try_merge_blocks(tlsf, block_root, -1)
#define try_merge_right(tlsf, block_root) try_merge_blocks(tlsf, block_root, 1)

/*
 * Try to find block_root's neighbours and merge them to one bigger continous block
 * of size equal to __block_size(block_root) + __block_size(found_neighbour).
 */
static page_frame_t *try_merge_blocks(tlsf_t *tlsf, page_frame_t *block_head, int side)
{
  page_frame_t *neighbour = NULL;
  tlsf_uint_t n_size, size;

  size = pages_block_size_get(block_head);
  /* get block's left neighbour */
  if ((side < 0) &&
      (pframe_number(block_head) > tlsf->owner->first_page_id)) {
    neighbour = __left_neighbour(block_head);
  }
  /* get block's right neighbour */
  else {
    if ((pframe_number(block_head) + pages_block_size_get(block_head)) <
        (tlsf->owner->first_page_id + tlsf->owner->total_pages)) {
      neighbour = __right_neighbour(block_head);
    }
  }
  if (!neighbour)
    goto out;

  n_size = pages_block_size_get(neighbour);

  /* if the result block is too big, merging can't be completed */
  if ((size + n_size) >= MAX_BLOCK_SIZE)
    goto out;

  /*
   * Wow, it seems that blocks can be successfully coalesced
   * So its neighbour may be safely removed from its indices.
   */
  pages_block_remove(tlsf, neighbour);
  pages_block_destroy(neighbour);
  pages_block_destroy(block_head);
  if (pframe_number(block_head) > pframe_number(neighbour)) {
    /*
     * neighbour is on the left of root_block, so neigbour's head page
     * will be the head of result block.
     */
    pages_block_create(neighbour, size + n_size - 1);
    block_head = neighbour;
  }
  else
    pages_block_create(block_head, size + n_size - 1);

  out:
  return block_head;
}

/*
 * Find suitable block which have size greater or equal to "size".
 * Strategy: "good fit".
 */
static page_frame_t *find_suitable_block(tlsf_t *tlsf, tlsf_uint_t size)
{
  struct tlsf_idxs ids;
  page_frame_t *block = NULL;
  list_node_t *n = NULL;

  /* Frist of all let's see if the are any blocks in the corresponding FLD */
  get_tlsf_ids(size, &ids);
  if (__fld_is_avail(tlsf, ids.fldi)) {
    /*
     * If there exists a block in the corresponding SLD and
     * if it has suitable size, we found out a suitable one.
     */
    if (__sld_is_avail(tlsf, ids.fldi, ids.sldi)) {
      tlsf_node_t *node = tlsf->map[ids.fldi].nodes + ids.sldi;

      if (node->max_avail_size >= size) {
        n = (size == (1 << __fldi2power(ids.fldi))) ?
          list_node_first(&node->blocks) : list_node_last(&node->blocks);
        goto found;
      }
    }

    /*
     * If there wasn't suitable block in the SLD that is tied with the "size",
     * try to check out first available block in the next SLD. (if exists)
     */
    ids.sldi = __find_next_sldi(tlsf, ids.fldi, ids.sldi);
    if (ids.sldi > 0) {
      n = list_node_first(&tlsf->map[ids.fldi].nodes[ids.sldi].blocks);
      goto found;
    }
  }

  /*
   * Otherwise, try to get first available block in the next
   * FLDi.
   */
  if ((ids.fldi = __find_next_fldi(tlsf, ids.fldi)) < 0)
    goto out;

  ids.sldi = bit_find_lsf(__sld_bitmap(tlsf, ids.fldi));
  n = list_node_first(&tlsf->map[ids.fldi].nodes[ids.sldi].blocks);

  found:
  block = list_entry(n, page_frame_t, node);

  out:
  return block;
}

#ifdef CONFIG_SMP
static int __put_page_to_cache(tlsf_t *tlsf, page_frame_t *page)
{
  tlsf_percpu_cache_t *pcpu = tlsf->percpu[cpu_id()];

  if (!pcpu || (pcpu->noc_pages >= TLSF_CPUCACHE_PAGES))
    return -1;

  preempt_disable();
  list_add2head(&pcpu->pages, &page->node);
  pcpu->noc_pages++;
  preempt_enable();

  return 0;
}

static page_frame_t *__get_page_from_cache(tlsf_t *tlsf)
{
  page_frame_t *page;
  tlsf_percpu_cache_t *pcpu = tlsf->percpu[cpu_id()];

  if (!pcpu || !pcpu->noc_pages)
    return NULL;

  preempt_disable();
  page = list_entry(list_node_first(&pcpu->pages), page_frame_t, node);
  list_del(&page->node);
  pcpu->noc_pages--;
  preempt_enable();

  return page;
}

#else
#define __put_page_to_cache(tlsf, page) -1
#define __get_page_from_cache(tlsf) NULL
#endif /* CONFIG_SMP */

static void tlsf_free_pages(page_frame_t *pages, page_idx_t num_pages, void *data)
{
  tlsf_t *tlsf = data;
  page_frame_t *merged_block;
  page_idx_t page_idx = pframe_number(pages);
  int i;

  ASSERT(num_pages != 0);
  if (((num_pages + page_idx - 1) < tlsf->owner->first_page_id) ||
      ((num_pages + page_idx - 1) >= (tlsf->owner->first_page_id + tlsf->owner->total_pages))) {
    kprintf(KO_WARNING "TLSF: Invalid size(%d) of freeing block starting from %d frame\n",
            num_pages, page_idx);
    kprintf(KO_WARNING "TLSF: Can't free %d pages starting from %d frame\n", num_pages, page_idx);
    return;
  }
  for (i = 0; i < num_pages; i++) {
#ifdef CONFIG_DEBUG_MM
    tlsf_uint_t flags = pages_block_flags(&pages[i]);
    if (pages[i].pool_type != tlsf->owner->type) {
      mm_pool_t *page_pool = get_mmpool_by_type(pages[i].pool_type);

      if (!page_pool) {
        panic("Page #%#x has invalid pool type: %d!",
              pframe_number(&pages[i]), pages[i].pool_type);
      }

      panic("Attemption to free page #%#x owened by pool %s to the pool %s!",
            pframe_number(&pages[i]), page_pool->name, tlsf->owner->name);
    }
    if ((flags & TLSF_PB_MASK) || !(flags & (1 << TLSF_PB_BUSY))) {
      panic("Attemption to free *already* free page #%#x to the pool %s! (%#x)",
            pframe_number(&pages[i]), tlsf->owner->name, pages[i]._private);
    }
#endif /* CONFIG_DEBUG_MM */

    bit_clear(&pages[i]._private, TLSF_PB_BUSY);
    clear_page_frame(page);
    list_del(&pages[i].chain_node);
  }
  if ((num_pages == 1) && !__put_page_to_cache(tlsf, pages))
    return;

  pages_block_create(pages, num_pages - 1);
  spinlock_lock(&tlsf->lock);
  merged_block = try_merge_left(tlsf, pages);
  merged_block = try_merge_right(tlsf, merged_block);
  pages_block_insert(tlsf, merged_block);
  spinlock_unlock(&tlsf->lock);
}

static page_frame_t *tlsf_alloc_pages(page_idx_t n, void *data)
{
  tlsf_t *tlsf = data;
  page_frame_t *block_head = NULL;
  tlsf_uint_t size;
  int i;

  if ((n >= MAX_BLOCK_SIZE) || (n > atomic_get(&tlsf->owner->free_pages)))
    goto out;
  if ((n == 1) && ((block_head = __get_page_from_cache(tlsf)) != NULL))
    goto init_block;

  spinlock_lock(&tlsf->lock);
  block_head = find_suitable_block(tlsf, n);
  if (!block_head) {
    spinlock_unlock(&tlsf->lock);
    goto out;
  }

  size = pages_block_size_get(block_head);
  pages_block_remove(tlsf, block_head);
  if (size > n) /* split block if necessary */
    pages_block_insert(tlsf, pages_block_split(tlsf, block_head, size - n));

  pages_block_destroy(block_head);
  spinlock_unlock(&tlsf->lock);

  init_block:
  /* Now we free to build pages chain and set TLSF_PB_BUSY bit for each allocated page */
  list_init_head(list_node2head(&block_head->chain_node));
  for (i = 0; i < n; i++) {
#ifndef CONFIG_DEBUG_MM
    bit_set(&block_head[i]._private, TLSF_PB_BUSY);
#else
    if (bit_test_and_set(&block_head[i]._private, TLSF_PB_BUSY))
      panic("Just allocated page frame #%#x is *already* busy! WTF?", pframe_number(block_head + i));
#endif /* CONFIG_DEBUG_MM */

    if (likely(i > 0))
      list_add_before(&block_head->chain_node, &block_head[i].chain_node);
  }

  out:
  return block_head;
}

/* initialize TLSF pages hierarchy */
static void build_tlsf_map(tlsf_t *tlsf)
{
  int i, j;
  page_idx_t npages = tlsf->owner->total_pages;
  page_frame_t *pages = pframe_by_number(tlsf->owner->first_page_id);

  /* initialize TLSF map */
  for (i = 0; i < TLSF_FLD_SIZE; i++) {
    tlsf_node_t *nodes = tlsf->map[i].nodes;
    tlsf->map[i].total_blocks = 0;

    for (j = 0; j < TLSF_SLD_SIZE; j++) {
      nodes[j].blocks_no = 0;
      list_init_head(&nodes[j].blocks);
      nodes[j].max_avail_size = 0;
    }
  }

  /* initialize bitmaps */
  memset(&tlsf->fld_bitmap, 0, sizeof(tlsf->fld_bitmap));
  memset(tlsf->slds_bitmap, 0, sizeof(*(tlsf->slds_bitmap)) * TLSF_SLD_BITMAP_SIZE);

  /*
   * Insert available blocks to the corresponding TLSF FLDs and SLDs.
   * All available pages are inserted one-by-one from left to right.
   */
  for (i = 0; i < npages; i++) {
    if (pages[i].flags & PF_RESERVED) /* skip reserved pages */
      continue;

    bit_set(&pages[i]._private, TLSF_PB_BUSY);
    list_init_head(list_node2head(&pages[i].chain_node));
    tlsf_free_pages(pages + i, 1, tlsf);
  }
}

static void tlsf_memdump(void *_tlsf)
{
  int i;
  tlsf_t *tlsf = _tlsf;

  for (i = 0; i < TLSF_FLD_SIZE; i++) {
    int size = 1 << __fldi2power(i);

    if (__fld_is_avail(tlsf, i)) {
      int j;

      kprintf("TLSF FLD %d (size %ld) is available\n", i, size);
      for (j = 0; j < TLSF_SLD_SIZE; j++) {
        struct tlsf_idxs ids = { i, j };
        tlsf_uint_t size2 = size_from_tlsf_ids(&ids);
        if (__sld_is_avail(tlsf, i, j)) {
          list_node_t *n;
          tlsf_node_t *node = tlsf->map[i].nodes + j;

          kprintf("...TLSF SLD %ld is available: %d\n", size2, node->blocks_no);
          kprintf("   sizes: ");
          list_for_each(&node->blocks, n)
            kprintf( "%d,", pages_block_size_get(list_entry(n, page_frame_t, node)));

          kprintf("\n");
        }
        else {
          kprintf("...TLSF SLD %ld is NOT available\n", size2);
        }
      }
    }
    else {
      kprintf("TLSF FLD %d (size %ld) is NOT available\n",
              i, size);
    }
  }
}

#ifdef CONFIG_SMP
#include <mm/slab.h>
#include <eza/smp.h>

static smp_hook_t __percpu_hook;
static int tlsf_smp_hook(cpu_id_t cpuid, void *_tlsf)
{
  tlsf_t *tlsf = _tlsf;
  tlsf_percpu_cache_t *cache = NULL;
  page_frame_t *pages, *p;
  int ret = -ENOMEM;
  list_head_t h;
  list_node_t *iter, *safe;

  if (likely(!idalloc_is_enabled()))
    cache = memalloc(sizeof(*cache));
  else
    cache = idalloc(sizeof(*cache));
  if (!cache)
    goto err;

  memset(cache, 0, sizeof(*cache));
  list_init_head(&cache->pages);
  list_init_head(&h);
  tlsf->percpu[cpuid] = cache;
  pages = tlsf_alloc_pages(TLSF_CPUCACHE_PAGES, tlsf);
  if (!pages)
    goto err;

  list_set_head(&h, &pages->chain_node);
  list_for_each_safe(&h, iter, safe) {
    p = list_entry(iter, page_frame_t, chain_node);
    list_del(&p->chain_node);
    bit_clear(&p->_private, TLSF_PB_BUSY);
    list_add2tail(&cache->pages, &p->node);
  }
  
  cache->noc_pages = TLSF_CPUCACHE_PAGES;
  return 0;

  err:
  if (cache && !idalloc_is_enabled()) {
    memfree(cache);
    tlsf->percpu[cpuid] = NULL;
  }

  return ret;
}

#endif /* CONFIG_SMP */

static inline void check_tlsf_defs(void)
{
  CT_ASSERT(TLSF_CPUCACHE_PAGES < MAX_BLOCK_SIZE);
  CT_ASSERT(TLSF_CPUCACHE_PAGES > 0);
  CT_ASSERT(TLSF_FLD_SIZE < TLSF_FLDS_MAX);
  CT_ASSERT(TLSF_FIRST_OFFSET > 0);
  CT_ASSERT(TLSF_SLD_BITMAP_SIZE < (sizeof(long) << 3));
  CT_ASSERT((FLD_FPOW2 + TLSF_FIRST_OFFSET) < (sizeof(long) << 3));
  CT_ASSERT(is_powerof2(TLSF_SLD_SIZE) &&
            (TLSF_SLD_SIZE <= ((1 << (FLD_FPOW2 + TLSF_FIRST_OFFSET)) >> 1)) &&
            (TLSF_SLD_SIZE >= TLSF_SLDS_MIN));
}

void tlsf_allocator_init(mm_pool_t *pool)
{
  tlsf_t *tlsf;
  long free_pages;

  tlsf = idalloc(sizeof(*tlsf));
  if (!tlsf)
    panic("Can not allocate %zd bytes for TLSF usign idalloc allocator!", sizeof(*tlsf));

  ASSERT(atomic_get(&pool->free_pages) > 0);
  memset(tlsf, 0, sizeof(*tlsf));
  check_tlsf_defs(); /* some paranoic checks */
  CT_ASSERT(sizeof(union tlsf_priv) <= sizeof(page_frames_array->_private));
  tlsf->owner = pool;
  spinlock_initialize(&tlsf->lock);

  /*
   * build_tlsf_map uses function __free_pages which
   * increments pool's free_pages counter. We don't
   * want counter becomes invalid.
   */
  free_pages = atomic_get(&pool->free_pages);
  atomic_set(&pool->free_pages, free_pages);
  pool->allocator.type = PFA_TLSF;
  pool->allocator.alloc_ctx = tlsf;
  pool->allocator.alloc_pages = tlsf_alloc_pages;
  pool->allocator.free_pages = tlsf_free_pages;
  pool->allocator.dump = tlsf_memdump;
  pool->allocator.max_block_size = MAX_BLOCK_SIZE;
  build_tlsf_map(tlsf);

#ifdef CONFIG_SMP
  memset(&__percpu_hook, 0, sizeof(__percpu_hook));
  __percpu_hook.hook = tlsf_smp_hook;
  __percpu_hook.arg = tlsf;
  __percpu_hook.name = "TLSF percpu";
  smp_hook_register(&__percpu_hook);
#endif /* CONFIG_SMP */

  kprintf("[MM] Pool \"%s\" initialized TLSF O(1) allocator\n", pool->name);
}

#ifdef CONFIG_DEBUG_MM
static void __validate_empty_sldi_dbg(tlsf_t *tlsf, int fldi, int sldi)
{
  /* Check if given SLD *really* hasn't any pages */
  tlsf_node_t *node = tlsf->map[fldi].nodes + sldi;
  list_node_t *n;

  list_for_each(&node->blocks, n) {
    panic("TLSF: (FLDi: %d, SLDi: %d) is marked as free, but it has at least "
          "one page frame[idx = %d] available!", fldi, sldi,
          pframe_number(list_entry(n, page_frame_t, node)));
  }
  if (node->blocks_no) {
    panic("TLSF: (FLDi: %d, SLDi: %d) is marked as free, but block_no field of "
          "corresponding TLSF node is not 0! (block_no = %d)", fldi, sldi, node->blocks_no);
  }
  if (node->max_avail_size) {
    panic("TLSF: (FLDi: %d, SLDi: %d) is marked as free, but max_avail_size field "
          "of corresponding TLSF node is %d instead of 0!", fldi, sldi, node->max_avail_size);
  }
}

void tlsf_validate_dbg(void *_tlsf)
{
  tlsf_t *tlsf = _tlsf;
  int fldi, sldi;
  page_idx_t total_pgs = 0;
  mm_pool_t *parent_pool = tlsf->owner;

  spinlock_lock(&tlsf->lock);
  for (fldi = 0; fldi < TLSF_FLD_SIZE; fldi++) {
    if (!__fld_is_avail(tlsf, fldi)) {
      /* Ok, given FLD is marked as free. Check if it's true */
      for (sldi = 0; sldi < TLSF_SLD_SIZE; sldi++) {
        if (!__sld_is_avail(tlsf, fldi, sldi))
          __validate_empty_sldi_dbg(tlsf, fldi, sldi);
      }

      /* It seems TLSF is not a lier (: */
    }
    for (sldi = 0; sldi < TLSF_SLD_SIZE; sldi++) {
      page_frame_t *pf;
      tlsf_node_t *node;
      list_node_t *n;
      int blocks = 0;

      if (!__sld_is_avail(tlsf, fldi, sldi)) {
        __validate_empty_sldi_dbg(tlsf, fldi, sldi);
        continue;
      }

      /*
       * Check if max_avail_size field and actual size of max block
       * in a directory are relevant.
       */
      node = tlsf->map[fldi].nodes + sldi;
      pf = list_entry(list_node_last(&node->blocks), page_frame_t, node);
      if (pages_block_size_get(pf) != node->max_avail_size) {
        panic("TLSF (FLDi: %d, SLDi: %d) TLSF node has invalid value in a max_avail_size field. "
              "max_avail_size = %d, actual block size = %d", fldi, sldi,
              node->max_avail_size, pages_block_size_get(pf));
      }

      /* Then check if actual number of blocks corresponds to blocks_no field */
      list_for_each(&node->blocks, n) {
        total_pgs += pages_block_size_get(list_entry(n, page_frame_t, node));
        blocks++;
      }
      if (blocks != node->blocks_no) {
        panic("TLSF (FLDi: %d, SLDi: %d) blocks_no field of TLSF node contains invalid value: %d "
              "but %d is expected!", fldi, sldi, node->blocks_no, blocks);
      }
    }
  }
  
#ifdef CONFIG_SMP
  {
    cpu_id_t c;

    for_each_cpu(c) {
      if (!tlsf->percpu[c])
        continue;
      
      ASSERT(tlsf->percpu[c]->noc_pages >= 0);
      total_pgs += tlsf->percpu[c]->noc_pages;
    }
  }
#endif /* CONFIG_SMP */
  
  if (total_pgs != atomic_get(&parent_pool->free_pages)) {
    panic("TLSF belonging to pool %s has inadequate number of free pages: "
          "%d, but pool itself tells us that there are %d pages available!",
          parent_pool->name, total_pgs, atomic_get(&parent_pool->free_pages));
  }

  spinlock_unlock(&tlsf->lock);
}
#endif /* CONFIG_DEBUG_MM */
