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
 * information TLSF holds in the _private
 * field of page_frame structure.
 * (note: size of _private field is 32 bits)
 */
union tlsf_priv {
  uint32_t pad;
  struct {
    uint16_t flags; /* TLSF-specific flags */
    uint16_t size;  /* size of pages block */
  };
};

/* TLSF main indices */
struct tlsf_idxs {
  int fldi; /* FLD index */
  int sldi; /* SLD index */
};

#define TLSF_PB_MARK  0x01 /* Each TLSF page must have this flag */
#define TLSF_PB_HEAD  0x02 /* Determines that a page is a block head */
#define TLSF_PB_TAIL  0x04 /* Determines that a page is a block tail */
#define TLSF_PB_MASK  0x07

/* First available power of two */
static const int FLD_FPOW2 = 0;

/* Very last power of two */
static const int FLD_LPOW2 = TLSF_FLD_SIZE + TLSF_FIRST_OFFSET - 2;
static const int MAX_BLOCK_SIZE = (1 << (TLSF_FLD_SIZE + TLSF_FIRST_OFFSET - 1));

static inline void get_tlsf_ids(uint16_t size, struct tlsf_idxs *ids);
static inline uint16_t size_from_tlsf_ids(struct tlsf_idxs *ids);

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

/*
 * All free TLSF pages are grouped into blocks. Block of size N
 * represents N continuos pages. TLSF only interested in block's
 * head(very first page) and its tail, so all we should to know
 * about the block is where its very first and very last pages
 * are located.
 * __make_block creates a block.
 */
static inline void __make_block(page_frame_t *head, page_frame_t *tail)
{
  list_init_node(&head->node);
  if (head != tail)
    list_init_node(&tail->node);

  head->node.next = &tail->node;
  tail->node.prev = &head->node;
}

/* get block size. */
static inline uint16_t __block_size(page_frame_t *block)
{
  union tlsf_priv priv = { block->_private };
  return priv.size;
}

static inline void __block_size_set(page_frame_t *block, uint16_t size)
{
  union tlsf_priv priv = { block->_private };

  priv.size = size;
  block->_private = priv.pad;
}

static inline uint16_t __block_flags(page_frame_t *block)
{
  union tlsf_priv priv = { block->_private };
  return priv.flags;
}

static inline void __block_flags_set(page_frame_t *block, uint16_t flags)
{
  union tlsf_priv priv = { block->_private };

  bits_or(&priv.flags, flags);
  block->_private = priv.pad;
}

static inline void __block_flags_set_mask(page_frame_t *block, uint16_t mask)
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
static void get_tlsf_ids(uint16_t size, struct tlsf_idxs *ids)
{
  int pow2 = bit_find_msf(size);

  ids->fldi = __power2fldi(pow2);
  ids->sldi = (size & ~(1 << pow2)) / __fld_offset(ids->fldi);
}

/* Get block size from its FLD and SLD indices */
static inline uint16_t size_from_tlsf_ids(struct tlsf_idxs *ids)
{
  uint16_t size = 1 << __fldi2power(ids->fldi);
  size += ids->sldi * __fld_offset(ids->fldi);

  return size;
}

static inline void __block_init(page_frame_t *block_root, uint16_t size)
{
  page_frame_t *tail = list_entry(block_root->node.next, page_frame_t, node);

  /*
   * "initialized" block must have PB_HEAD and PB_TAIL flags set
   * on its frist and the last page respectively.
   */
  bit_set(&block_root->_private, bitnumber(TLSF_PB_HEAD));
  bit_set(&tail->_private, bitnumber(TLSF_PB_TAIL));
  __block_size_set(block_root, size);
}

static inline void __block_deinit(page_frame_t *block_root)
{
  page_frame_t *tail = list_entry(block_root->node.next, page_frame_t, node);
  
  bit_clear(&block_root->_private, bitnumber(TLSF_PB_HEAD));
  bit_clear(&tail->_private, bitnumber(TLSF_PB_TAIL));
  list_init_head(&block_root->head);  
  list_init_node(&block_root->node);
  if (block_root != tail)
    list_init_node(&tail->node);
}

/*
 * insert a block to the TLSF structures and
 * update FLD and SLD bits if necessary.
 */
static void __block_insert(tlsf_t *tlsf, page_frame_t *block_root,
                                  uint16_t size, struct tlsf_idxs *ids)
{
  tlsf_node_t *sld_node;
  sld_node = tlsf->map[ids->fldi].nodes + ids->sldi;

  /* The greates block should be the last block in blocks list */
  if (size > sld_node->max_avail_size) {
    list_add2tail(&sld_node->blocks, list_head(&block_root->head));
    sld_node->max_avail_size = size;
  }
  else
    list_add2head(&sld_node->blocks, list_head(&block_root->head));

  /*
   * Update bitmaps if new block has became the first available
   * block in the given SLD...
   */
  if (sld_node->blocks_no++ == 0) {
    __sld_mark_avail(tlsf, ids->fldi, ids->sldi);
    
    /* Also it might be the first one in the corresponding FLD */
    if (tlsf->map[ids->fldi].total_blocks++ == 0)
      __fld_mark_avail(tlsf, ids->fldi);
  }
}

/*
 * Remove block from the TLSF structures and
 * update TLSF internal bits and counters if necessary.
 */
static void __block_remove(tlsf_t *tlsf, page_frame_t *block_root,
                           uint16_t size, struct tlsf_idxs *ids)
{
  tlsf_node_t *sld_node;

  list_cut_head(&block_root->head);
  sld_node = tlsf->map[ids->fldi].nodes + ids->sldi;
  if (--sld_node->blocks_no == 0) {
    /*
     * We've just removed the last available pages block
     * with given FLD and SLD indices from the TLSF blocks set.
     * So, it's a time to let other know about it.
     */

    __sld_mark_unavail(tlsf, ids->fldi, ids->sldi);
    sld_node->max_avail_size = 0;
    if (--tlsf->map[ids->fldi].total_blocks == 0) {
      /* Unfortunatelly that was the last available SLD node on given FLD */
      __fld_mark_unavail(tlsf, ids->fldi);
    }
  }
  else {
    if (sld_node->max_avail_size == __block_size(block_root)) {
      sld_node->max_avail_size =
        __block_size(list_entry(list_node_last(&sld_node->blocks), page_frame_t, head));
    }
  }
}

static void block_insert(tlsf_t *tlsf, page_frame_t *block_root, uint16_t size)
{
  struct tlsf_idxs ids;

  __block_init(block_root, size);
  get_tlsf_ids(size, &ids);
  __block_insert(tlsf, block_root, size, &ids);
}

static void block_remove(tlsf_t *tlsf, page_frame_t *block_root)
{
  struct tlsf_idxs ids;
  uint16_t size = __block_size(block_root);

  get_tlsf_ids(size, &ids);  
  __block_remove(tlsf, block_root, size, &ids);
  __block_deinit(block_root);
}

/*
 * Get left neighbour of the block "block_root".
 * Left neighbour is a block that has
 * physical address of its first page leaster than an address of
 * "head" page of the block_root.
 * return NULL left neighbour wasn't found.
 */
static inline page_frame_t *__left_neighbour(page_frame_t *block_root)
{
  page_frame_t *page;

  page = pframe_by_number(pframe_number(block_root) - 1);
  if (!bit_test(&page->_private, bitnumber(TLSF_PB_TAIL)))
    return NULL;

  return list_entry(page->node.prev, page_frame_t, node);
}

/*
 * Get right neighbour of the "block_root" or return NULL if neighbour
 * wasn't found.
 * Right neighbour is a block that has physical address of its tail head page
 * greater than an address of tail page of block_root.
 */
static inline page_frame_t *__right_neighbour(page_frame_t *block_root)
{
  page_frame_t *page;

  page = pframe_by_number(pframe_number(list_entry(block_root->node.next,
                                                   page_frame_t, node) + 1));
  if (!bit_test(&page->_private, bitnumber(TLSF_PB_HEAD)))
    return NULL;
  
  return page;
}

/*
 * Split block "block_root" into two new blocks. The head of block with size
 * (__block_size(block_root) - split_size) will be equal to current head of block_root
 * The head of new block that is cutted from original block root will have size
 * split_size and its root page is a page after tail page of new block_root.
 */
static page_frame_t *split(tlsf_t *tlsf, page_frame_t *block_root, uint16_t split_size)
{
  int offset = __block_size(block_root) - split_size;
  page_frame_t *new_block;

  new_block = pframe_by_number(pframe_number(block_root) + offset);
  __make_block(new_block, pframe_by_number(pframe_number(new_block) + split_size - 1));  
  __make_block(block_root, pframe_by_number(pframe_number(block_root) + offset - 1));  
  __block_size_set(block_root, offset);
  bit_set(&new_block->_private, bitnumber(TLSF_PB_MARK));

  return new_block;
}

#define try_merge_left(tls, block_root)  try_merge(tlsf, block_root, -1)
#define try_merge_right(tlsf, block_root) try_merge(tlsf, block_root, 1)

/*
 * Try to find block_root neighbours and merge them to one block with
 * size equal to __block_size(block_root) + __block_size(found_neighbour)
 */
static page_frame_t *try_merge(tlsf_t *tlsf, page_frame_t *block_root, int side)
{
  page_frame_t *neighbour;
  uint16_t n_size, n_flags, size;
  
  size = __block_size(block_root);
  if (side < 0) { /* get block's left neighbour */
    if (pframe_number(block_root) <= tlsf->first_page_idx)
      goto out;
    
    neighbour = __left_neighbour(block_root);
  }
  else { /* get block's right neighbour */
    if ((pframe_number(block_root) + size) > tlsf->last_page_idx)
      goto out;

    neighbour = __right_neighbour(block_root);
  }
  if (!neighbour)
    goto out;

  n_size = __block_size(neighbour);
  n_flags = __block_flags(neighbour);

  /* if the result block is too big, merging can't be completed */
  if ((size + n_size) >= MAX_BLOCK_SIZE)
    goto out;

  /*
   * Wow, it seems that blocks can be successfully coalesced
   * So its neighbour may be safely removed from its indices.
   */
  block_remove(tlsf, neighbour);
  if (pframe_number(block_root) > pframe_number(neighbour)) {
    /*
     * neighbour is on the left of root_block, so neigbour's head page
     * will be the head of result block.
     */
    page_frame_t *tmp = block_root;

    block_root = neighbour;
    neighbour = tmp;
  }

  __block_size_set(neighbour, 0);
  bit_clear(&neighbour->_private, bitnumber(TLSF_PB_MARK));
  __make_block(block_root, neighbour);
  __block_size_set(block_root, size + n_size);
  
  out:
  return block_root;
}

/*
 * Find suitable block which have size greater or equal to "size".
 * Strategy: "good fit".
 */
static page_frame_t *find_suitable_block(tlsf_t *tlsf, uint16_t size)
{
  struct tlsf_idxs ids;
  page_frame_t *block = NULL;
  list_node_t *n = NULL;

  /* Frist of all let's see if the is any blocks in the corresponding FLD */
  get_tlsf_ids(size, &ids);
  if (__fld_is_avail(tlsf, ids.fldi)) {
    /*
     * If there exists a block in the corresponding SLD and
     * if it has suitable size, all is ok.
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
     * If there wasn't suitable block in the SLD that ties with "size",
     * try to get first available block in the next SLD. (if exists)
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
  block = list_entry(n, page_frame_t, head);
  
  out:
  return block;
}

static void __free_pages(page_frame_t *pages, page_idx_t num_pages, void *data)
{
  tlsf_t *tlsf = data;
  page_frame_t *merged_block;
  mm_pool_t *pool = mmpools_get_pool(tlsf->owner);
  int n;
  page_idx_t page_idx = pframe_number(pages);

  ASSERT(num_pages != 0);
  spinlock_lock(&tlsf->lock);
  n = __block_size(pages);
  if (((n + page_idx - 1) < tlsf->first_page_idx) ||
      ((n + page_idx - 1) > tlsf->last_page_idx)) {
    kprintf(KO_WARNING "TLSF: Invalid size(%d) of freeing block starting from %d\n",
            n, page_idx);
    goto cant_free;
  }
  if (!bit_test(&pages->_private, bitnumber(TLSF_PB_MARK))) {
    kprintf(KO_WARNING "TLSF: Freeing page frame #%d is NOT a TLSF frame!\n", page_idx);
    goto cant_free;
  }
  if (bit_test(&pages->_private, bitnumber(TLSF_PB_HEAD))) {
    kprintf(KO_WARNING "TLSF: Attemption to free already freed page %d!\n", page_idx);
    goto cant_free;
  }
  if (unlikely(num_pages > n)) {
    kprintf(KO_WARNING "TLSF: Detected an attemption to free more pages(%d) "
            "than given block contains(%d).\n"
            "Default behaivour: free %d pages\n", num_pages, n, n);
    num_pages = n;
  }
  else if (num_pages < n) {
    /*
     * User always may free less pages than block actually has.
     * In this case we split freeing block into two parts. The
     * first one(starting from the very first page in a block) will be
     * fried and the second one won't be touched(except setting new block size)
     */

    page_frame_t *pf = pages + num_pages;
    
    bit_set(&pf->_private, bitnumber(TLSF_PB_MARK));
    __block_size_set(pf, n - num_pages);
    __block_size_set(pages, num_pages);
  }
  
  __make_block(pages, pages + num_pages - 1);  
  merged_block = try_merge_left(tlsf, pages);
  merged_block = try_merge_right(tlsf, merged_block);
  block_insert(tlsf, merged_block, __block_size(merged_block));
  atomic_add(&pool->free_pages, num_pages);
  goto out;

  cant_free:
  kprintf(KO_WARNING "TLSF: Can't free pages starting from %d frame\n", page_idx);
  
  out:
  spinlock_unlock(&tlsf->lock);
}

static page_idx_t __get_pblock_size(page_frame_t *pages_block_start, void *data)
{
  page_idx_t ret = 0;
  tlsf_t *tlsf = data;
  
  spinlock_lock(&tlsf->lock);
  if (!bit_test(&pages_block_start->_private, bitnumber(TLSF_PB_MARK))) {
    kprintf(KO_WARNING "TLSF: Attemption to get size of non-tlsf block, frame #%d\n",
            pframe_number(pages_block_start));
    ret = -EINVAL;
  }
  else {
    if (bit_test(&pages_block_start->_private, bitnumber(TLSF_PB_HEAD))) {
      kprintf(KO_WARNING "TLSF: Attemption to get a size of free TLSF pages block, frame %d\n",
              pframe_number(pages_block_start));
      ret = -EINVAL;
    }
    else
      ret = __block_size(pages_block_start);
  }
  
  spinlock_unlock(&tlsf->lock);
  return ret;
}

static page_frame_t *__alloc_pages(int n, void *data)
{
  tlsf_t *tlsf = data;
  page_frame_t *block_root = NULL;
  uint16_t size;
  mm_pool_t *pool = mmpools_get_pool(tlsf->owner);

  if (n >= MAX_BLOCK_SIZE)
    goto out;
  
  spinlock_lock(&tlsf->lock);  
  block_root = find_suitable_block(tlsf, n);  
  if (!block_root) {
    spinlock_unlock(&tlsf->lock);
    goto out;
  }

  block_remove(tlsf, block_root);
  size = __block_size(block_root);
  if (size > n) /* split block if necessary */
    block_insert(tlsf, split(tlsf, block_root, size - n), size - n);

  atomic_sub(&pool->free_pages, n);
  spinlock_unlock(&tlsf->lock);
  
  out:  
  return block_root;
}

/* initialize TLSF pages hierarchy */
static void build_tlsf_map(tlsf_t *tlsf, page_frame_t *pages, page_idx_t npages)
{
  int i, j;

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
    
  tlsf->first_page_idx = tlsf->last_page_idx = - 1;
  /* initialize bitmaps */
  memset(&tlsf->fld_bitmap, 0, sizeof(tlsf->fld_bitmap));
  memset(tlsf->slds_bitmap, 0, sizeof(*(tlsf->slds_bitmap)) * TLSF_SLD_BITMAP_SIZE);

  /*
   * Insert available blocks to the corresponding TLSF FLDs and SLDs.
   * All available pages are inserted one-by-one from right to left.
   * __free_pages will merge all continuos pages. Typically page blocks
   * are splitted from left to right. I.e. page block header is always
   * the lefmost page in the block, so it's better to free pages starting
   * from the last one.
   */
  for (i = npages - 1; i >= 0; i--) {
    if (pages[i].flags & PF_RESERVED) /*  */
      continue;
    if (tlsf->last_page_idx < 0)
      tlsf->last_page_idx = pframe_number(pages + i);
    
    tlsf->first_page_idx = pframe_number(pages + i);
    bit_set(&pages[i]._private, bitnumber(TLSF_PB_MARK));
    __block_size_set(pages + i, 1);
    __free_pages(pages + i, 1, tlsf);
  }
}

static void check_tlsf_defs(void)
{
  ASSERT(TLSF_FLD_SIZE < TLSF_FLDS_MAX);
  ASSERT(TLSF_FIRST_OFFSET > 0);
  ASSERT(TLSF_SLD_BITMAP_SIZE < (sizeof(long) << 3));
  ASSERT((FLD_FPOW2 + TLSF_FIRST_OFFSET) < (sizeof(long) << 3));
  ASSERT(is_powerof2(TLSF_SLD_SIZE) &&
         (TLSF_SLD_SIZE <= ((1 << (FLD_FPOW2 + TLSF_FIRST_OFFSET)) >> 1)) &&
         (TLSF_SLD_SIZE >= TLSF_SLDS_MIN));
}

void tlsf_alloc_init(mm_pool_t *pool)
{
  tlsf_t *tlsf = idalloc(sizeof(*tlsf));
  long free_pages;

  ASSERT(pool->free_pages > 0);
  memset(tlsf, 0, sizeof(*tlsf));
  check_tlsf_defs(); /* some paranoic checks */
  CT_ASSERT(sizeof(union tlsf_priv) == sizeof(page_frames_array->_private));
  tlsf->owner = pool->type;
  spinlock_initialize(&tlsf->lock);

  /*
   * build_tlsf_map uses function __free_pages which
   * increments pool's free_pages counter. We don't
   * want counter becomes invalid.
   */
  free_pages = atomic_get(&pool->free_pages);
  build_tlsf_map(tlsf, pool->pages, pool->total_pages);
  atomic_set(&pool->free_pages, free_pages);
  pool->allocator.type = PFA_TLSF;
  pool->allocator.alloc_ctx = tlsf;
  pool->allocator.alloc_pages = __alloc_pages;
  pool->allocator.free_pages = __free_pages;
  pool->allocator.pages_block_size = __get_pblock_size;
  pool->allocator.block_sz_min = 1;
  pool->allocator.block_sz_max = MAX_BLOCK_SIZE;
  kprintf("[MM] Pool \"%s\" initialized TLSF O(1) allocator\n",
          mmpools_get_pool_name(pool->type));
}

#ifdef CONFIG_DEBUG_MM
void tlsf_memdump(void *_tlsf)
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
        uint16_t size2 = size_from_tlsf_ids(&ids);
        if (__sld_is_avail(tlsf, i, j)) {
          list_node_t *n;
          tlsf_node_t *node = tlsf->map[i].nodes + j;
          
          kprintf("...TLSF SLD %ld is available: %d\n", size2, node->blocks_no);
          kprintf("   sizes: ");
          list_for_each(&node->blocks, n)
            kprintf( "%d,", __block_size(list_entry(n, page_frame_t, head)));

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
#endif /* CONFIG_DEBUG_MM */
