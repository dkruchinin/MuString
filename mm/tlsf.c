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

#include <ds/list.h>
#include <mlibc/string.h>
#include <mlibc/assert.h>
#include <mlibc/stddef.h>
#include <mlibc/bitwise.h>
#include <mm/page.h>
#include <mm/mmpool.h>
#include <mm/pfalloc.h>
#include <mm/idalloc.h>
#include <mm/tlsf.h>
#include <eza/spinlock.h>
#include <eza/arch/atomic.h>
#include <eza/arch/types.h>

union tlsf_priv {
  uint32_t pad;
  struct {
    uint16_t flags;
    uint16_t size;    
  };
};

struct tlsf_idxs {
  int fldi;
  int sldi;
};

#define TLSF_PB_HEAD 0x01
#define TLSF_PB_TAIL 0x02
#define TLSF_PB_FREE 0x04
#define TLSF_PB_MASK 0x07

static const int FLD_FPOW2 = 0;
static const int FLD_LPOW2 = TLSF_FLD_SIZE + TLSF_FIRST_OFFSET - 2;
static const int MAX_BLOCK_SIZE = (1 << (TLSF_FLD_SIZE + TLSF_FIRST_OFFSET - 1));

static inline void get_tlsf_ids(uint16_t size, struct tlsf_idxs *ids);
static inline uint16_t size_from_tlsf_ids(struct tlsf_idxs *ids);

#define __bitno(pow2)      ((pow2) >> 1)
#define __fld_offset(fldi) ((fldi) ? (TLSF_SLD_SIZE << ((fldi) - 1)) : TLSF_SLD_SIZE)
#define __fldi(size)       (__power2fldi(bit_find_msf(size)))
#define __sldi(size, fldi) (((size) & ~(1 << __fldi2power(fldi)) /  __fld_offset(fldi))
#define __fldi2power(fldi) ((fldi) ? ((fldi) + TLSF_FIRST_OFFSET - 1) : 0)
#define __power2fldi(pow)                                               \
  (((pow) >= TLSF_FIRST_OFFSET) ? ((pow) - TLSF_FIRST_OFFSET + 1) : 0)
#define __sld_bitmap(tlsf, fldi) (*(tlsf_bitmap_t *)((tlsf)->slds_bitmap + (fldi) * TLSF_SLD_SIZE))

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

static inline int __find_next_fldi(tlsf_t *tlsf, int start)
{
  return bit_find_lsf(tlsf->fld_bitmap & ~((1 << (start + 1)) - 1));
}

static inline int __find_next_sldi(tlsf_t *tlsf, int fldi, int sldi_start)
{
  return
    bit_find_lsf(__sld_bitmap(tlsf, fldi) & ~((1 << (sldi_start + 1)) - 1));
}

static void get_tlsf_ids(uint16_t size, struct tlsf_idxs *ids)
{
  int pow2 = bit_find_msf(size);

  ids->fldi = __power2fldi(pow2);
  ids->sldi = (size & ~(1 << pow2)) / __fld_offset(ids->fldi);
}

static inline uint16_t size_from_tlsf_ids(struct tlsf_idxs *ids)
{
  uint16_t size = 1 << __fldi2power(ids->fldi);
  size += ids->sldi * __fld_offset(ids->fldi);

  return size;
}

static inline void __block_init(page_frame_t *block_root, uint16_t size)
{
  page_frame_t *tail = list_entry(block_root->node.prev, page_frame_t, node);
  
  __block_flags_set_mask(block_root, ~TLSF_PB_MASK);
  __block_flags_set(block_root, TLSF_PB_FREE | TLSF_PB_HEAD);
  bit_clear(&tail->_private, __bitno(TLSF_PB_TAIL));
  __block_size_set(block_root, size);
}

static inline void __block_deinit(page_frame_t *block_root)
{
  page_frame_t *tail = list_entry(block_root->node.prev, page_frame_t, node);

  bit_clear(&block_root->_private, __bitno(TLSF_PB_FREE));
  bit_set(&tail->_private, __bitno(TLSF_PB_TAIL));
  list_init_head(&block_root->head);
}

static void __block_insert(tlsf_t *tlsf, page_frame_t *block_root,
                                  uint16_t size, struct tlsf_idxs *ids)
{
  tlsf_node_t *sld_node;

  sld_node = tlsf->map[ids->fldi].nodes + ids->sldi;
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
    if (tlsf->map[ids->fldi].avail_nodes++ == 0)
      __fld_mark_avail(tlsf, ids->fldi);
  }
}

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
    if (--tlsf->map[ids->fldi].avail_nodes == 0) {
      /* Unfortunatelly that was the last available SLD node on given FLD */
      __fld_mark_unavail(tlsf, ids->fldi);
    }
  }
  else {
    sld_node->max_avail_size =
      __block_size(list_entry(list_node_last(&sld_node->blocks), page_frame_t, head));
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

static inline page_frame_t *__left_neighbour(page_frame_t *block_root)
{
  page_frame_t *page;

  page = pframe_by_number(pframe_number(block_root) - 1);
  if (bit_test(&page->_private, __bitno(TLSF_PB_TAIL)))
    return list_entry(page->node.next->next, page_frame_t, node);

  return list_entry(page->node.next, page_frame_t, node);
}

static inline page_frame_t *__right_neighbour(page_frame_t *block_root)
{
  page_frame_t *page;

  page = list_entry(block_root->node.prev, page_frame_t, node);  
  return pframe_by_number(pframe_number(page) + 1);
}

static page_frame_t *split(tlsf_t *tlsf, page_frame_t *block_root, uint16_t split_size)
{
  page_frame_t *new_block;

  new_block = pframe_by_number(pframe_number(block_root) + split_size);
  list_cut_sublist(&new_block->node, block_root->node.prev);
  __block_size_set(block_root, split_size);
  __block_deinit(block_root);

  return new_block;
}

#define try_merge_left(tls, block_root)  try_merge(tlsf, block_root, -1)
#define try_merge_right(tlsf, block_root) try_merge(tlsf, block_root, 1)

static page_frame_t *try_merge(tlsf_t *tlsf, page_frame_t *block_root, int side)
{
  page_frame_t *neighbour;
  uint16_t n_size, n_flags, size;
  struct tlsf_idxs n_ids;

  size = __block_size(block_root);
  if (side < 0) { /* get page block left neighbour */
    if (pframe_number(block_root) <= tlsf->first_page_idx)
      goto out;
    
    neighbour = __left_neighbour(block_root);
  }
  else { /* get page block right neighbour */
    if ((pframe_number(block_root) + size) >= tlsf->last_page_idx)
      goto out;

    neighbour = __right_neighbour(block_root);
  }
  
  n_size = __block_size(neighbour);
  n_flags = __block_flags(neighbour);
  get_tlsf_ids(n_size, &n_ids);
  /*
   * Unfortunately our block can not be coalesced with its neighbour
   * if either block_root's neighbour is not a head of another block
   * or result block seems to be too large to seat in the set of free blocks.
   */
  if (!bit_test(&n_flags, __bitno(TLSF_PB_HEAD)) ||
      !bit_test(&n_flags, __bitno(TLSF_PB_FREE)) ||
      ((size + n_size) >= MAX_BLOCK_SIZE)) {
    goto out;
  }

  /* Wow, it seems that blocks can be successfully coalesced */
  block_remove(tlsf, neighbour);  
  if (pframe_number(block_root) < pframe_number(neighbour)) {
    list_add_range(&neighbour->node, neighbour->node.prev,
                   block_root->node.prev, &block_root->node);    
  }
  else {
    list_add_range(&block_root->node, block_root->node.prev,
                   neighbour->node.prev, &neighbour->node);
    block_root = neighbour;
  }

  __block_size_set(block_root, size + n_size);  
  
  out:
  return block_root;
}

static page_frame_t *find_suitable_block(tlsf_t *tlsf, uint16_t size)
{
  struct tlsf_idxs ids;
  page_frame_t *block = NULL;
  list_node_t *n = NULL;
  
  get_tlsf_ids(size, &ids);
  if (__fld_is_avail(tlsf, ids.fldi)) {
    if (__sld_is_avail(tlsf, ids.fldi, ids.sldi)) {
      tlsf_node_t *node = tlsf->map[ids.fldi].nodes + ids.sldi;
      if (node->max_avail_size >= size) {
        n = (size == (1 << __fldi2power(ids.fldi))) ?
          list_node_first(&node->blocks) : list_node_last(&node->blocks);
        goto found;
      }
    }

    ids.sldi = __find_next_sldi(tlsf, ids.fldi, ids.sldi);
    if (ids.sldi > 0) {
      n = list_node_first(&tlsf->map[ids.fldi].nodes[ids.sldi].blocks);
      goto found;
    }
  }
  if ((ids.fldi = __find_next_fldi(tlsf, ids.fldi)) < 0)
    goto out;

  ids.sldi = bit_find_lsf(__sld_bitmap(tlsf, ids.fldi));
  n = list_node_first(&tlsf->map[ids.fldi].nodes[ids.sldi].blocks);
  
  found:
  block = list_entry(n, page_frame_t, head);
  
  out:
  return block;
}

static void __free_pages(page_frame_t *pages, int n, void *data)
{
  tlsf_t *tlsf = data;
  page_frame_t *merged_block;
  mm_pool_t *pool = mmpools_get_pool(tlsf->owner);

  if (bit_test(&pages->_private, __bitno(TLSF_PB_FREE))) {
    return;
  }
  
  spinlock_lock(&tlsf->lock);
  list_cut_head(&pages->head);
  merged_block = try_merge_left(tlsf, pages);
  merged_block = try_merge_right(tlsf, merged_block);
  block_insert(tlsf, merged_block, __block_size(merged_block));
  spinlock_unlock(&tlsf->lock);
  atomic_add(&pool->free_pages, n);
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
  if (size > n) { /* split block if necessary */
    page_frame_t *tmp = split(tlsf, block_root, n);
    block_insert(tlsf, tmp, size - n);
  }

  list_set_head(&block_root->head, &block_root->node);
  spinlock_unlock(&tlsf->lock);
  atomic_sub(&pool->free_pages, n);
  
  out:  
  return block_root;
}

static void build_tlsf_map(tlsf_t *tlsf, list_head_t *pages, page_idx_t npages)
{
  int block_size = 1 << FLD_LPOW2;
  page_idx_t blocks = npages / block_size;
  page_idx_t pages_rest = npages - (blocks * block_size);
  int i, j;
  struct tlsf_idxs ids;

  kprintf("TLSF: %ld blocks of %ld size\n", blocks, block_size);
  /* initialize TLSF map */
  for (i = 0; i < TLSF_FLD_SIZE; i++) {
    tlsf_node_t *nodes = tlsf->map[i].nodes;    
    tlsf->map[i].avail_nodes = 0;
    
    for (j = 0; j < TLSF_SLD_SIZE; j++) {
      nodes[j].blocks_no = 0;
      list_init_head(&nodes[j].blocks);
      nodes[j].max_avail_size = 0;
    }
  }

  tlsf->first_page_idx =
    pframe_number((page_frame_t *)list_entry(list_node_first(pages), page_frame_t, node));
  tlsf->last_page_idx =
    pframe_number((page_frame_t *)list_entry(list_node_last(pages), page_frame_t, node));
  
  /* initialize bitmaps */
  memset(&tlsf->fld_bitmap, 0, sizeof(tlsf->fld_bitmap));
  memset(tlsf->slds_bitmap, 0, sizeof(*(tlsf->slds_bitmap)) * TLSF_SLD_BITMAP_SIZE);

  /* insert available blocks to the corresponding TLSF map entries */
  get_tlsf_ids(block_size, &ids);
  while (blocks-- > 0) {
    page_frame_t *first_page = list_entry(list_node_first(pages), page_frame_t, node);
    page_frame_t *last_page = pframe_by_number(pframe_number(first_page) + block_size - 1);

    list_cut_sublist(&first_page->node, &last_page->node);
    block_insert(tlsf, first_page, block_size);
  }
  if (pages_rest > 0) {
    page_frame_t *last_block = list_entry(list_node_first(pages), page_frame_t, node);
    mm_pool_t *pool = mmpools_get_pool(tlsf->owner);

    list_cut_head(pages);
    __block_init(last_block, pages_rest);
    bit_clear(&last_block->_private, __bitno(TLSF_PB_FREE));
    atomic_sub(&pool->free_pages, pages_rest);
    __free_pages(last_block, pages_rest, tlsf);
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
  
  memset(tlsf, 0, sizeof(*tlsf));
  check_tlsf_defs();
  tlsf->owner = pool->type;
  spinlock_initialize(&tlsf->lock, "TLSF spinlock");
  build_tlsf_map(tlsf, &pool->pages->head, pool->free_pages);
  pool->pages = NULL;
  pool->allocator.type = PFA_TLSF;
  pool->allocator.alloc_ctx = tlsf;
  pool->allocator.alloc_pages = __alloc_pages;
  pool->allocator.free_pages = __free_pages;
}

#ifdef TLSF_DEBUG
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
          tlsf_node_t *node = tlsf->map[i].nodes + j;
          kprintf("...TLSF SLD %ld is available: %d\n", size2, node->blocks_no);
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
#endif /* TLSF_DEBUG */
