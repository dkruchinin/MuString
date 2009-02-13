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
 * (c) Copyright 2009 Dan Kruchinin <dan.kruchinin@gmail.com>
 *
 * ds/idx_allocator.c: Index allocator implementaion.
 *
 */

#include <ds/idx_allocator.h>
#include <mm/page.h>
#include <mm/pfalloc.h>
#include <mm/slab.h>
#include <mlibc/string.h>
#include <mlibc/stddef.h>
#include <mlibc/assert.h>
#include <mlibc/types.h>

/*
 * Main idea of index allocator is quite simple:
 * There is a group of tasks that requires dynamic allocation
 * of unique non-negative integer identifiers. Where a set of such
 * integers is relatively big (thousands of numbers), index allocator
 * becomes a nifty solution.
 * The core of allocator is two bitmaps. One of them called "indices bitmap"
 * or "second level bitmap". Each bit of that bitmap corresponds to particular unique
 * index. Since second-level bitmap is a large portion of continuous memory, searching
 * of set/zero bit in it becomes relatively slow. For speeding-up search process we invented
 * a first-level bitmap. Second-level array is splitted on groups each of them contains BYTES_PER_ITEM bytes.
 * First-level(or main) bitmap establishes corresponding between particular group and its
 * state(group may have free indices or not).
 * So index allocation assumes at first searching of group index in a the first-level bitmap and only
 * then searching an index in the second level bitmap starting from a group index.
 */

#define MIN_IDA_SIZE (WORDS_PER_ITEM * sizeof(ulong_t))

static inline size_t __get_main_bmap_size(idx_allocator_t *ida)
{
  if (likely(ida->ids_bmap))
    return (round_up_pow2((ida->size * sizeof(ulong_t)) / BYTES_PER_ITEM));

  return ida->size;
}

static ulong_t alloc_id_large_ida(idx_allocator_t *ida)
{
  ulong_t id = IDX_INVAL;
  long fnfi;
  size_t i, main_offs, main_sz;

  main_sz = __get_main_bmap_size(ida) / sizeof(ulong_t);
  i = 0;  
  for (;;) {
    while (i < main_sz) {
      fnfi = zero_bit_find_lsf(ida->main_bmap[i]);
      if (fnfi >= 0) {
        fnfi = (fnfi * WORDS_PER_ITEM) + i * WORDS_PER_ITEM * BITS_PER_LONG;
        main_offs = i;
        break;
      }

      i++;
    }
    if ((fnfi >= 0) && (fnfi < ida->size)) {
      int res_id, j, total_sz;

      total_sz = fnfi + WORDS_PER_ITEM;      
      for (j = fnfi; j < total_sz; j++) {
        res_id = zero_bit_find_lsf(ida->ids_bmap[j]);
        if (res_id < 0)
          continue;

        bit_set(ida->ids_bmap + j, res_id);    
        id = res_id + j * BITS_PER_LONG;
        if (id >= ida->max_id) {
          bit_clear(ida->ids_bmap + j, res_id);
          id = IDX_INVAL;
        }
        
        goto out;
      }

      bit_set(ida->main_bmap + main_offs,
              (fnfi - (main_offs * WORDS_PER_ITEM * BITS_PER_LONG)) / WORDS_PER_ITEM);
      if ((ida->main_bmap[i] & ~0UL) == ~0UL)
        i++;
    }
    else
      break;
  }

  out:
  return id;
}

static ulong_t alloc_id_small_ida(idx_allocator_t *ida)
{
  ulong_t id = IDX_INVAL;
  long bit;
  size_t i, sz;

  sz = __get_main_bmap_size(ida) / sizeof(ulong_t);
  i = 0;
  while (i < sz)  {
    bit = zero_bit_find_lsf(ida->main_bmap[i]);
    if (bit >= 0) {
      bit_set(ida->main_bmap + i, bit);
      id = bit + i * BITS_PER_LONG;
      if (id >= ida->max_id) {
        bit_clear(ida->main_bmap + i, bit);
        id = IDX_INVAL;
      }
    }

    i++;
  }

  return id;
}

static void reserve_id(idx_allocator_t *ida, ulong_t idx)
{
  int start_id, bitno;
  ulong_t *ptr;
  
  ASSERT(idx < ida->max_id);
  start_id = idx / BITS_PER_LONG;
  bitno = idx - start_id * BITS_PER_LONG;
  if (likely(ida->ids_bmap))
    ptr = ida->ids_bmap + start_id;    
  else
    ptr = ida->main_bmap + start_id;
  if (bit_test_and_set(ptr, bitno)) {
    kprintf(KO_WARNING "Detected an attempt to reserve already busy index %d "
            "[function: %s]!\n", idx, __FUNCTION__);
  }
}

static void free_id(idx_allocator_t *ida, ulong_t idx)
{
  int start_id, bitno, main_id, main_bitno;
  ulong_t *ptr;

  ASSERT(idx < ida->max_id);
  start_id = idx / BITS_PER_LONG;
  bitno = idx - start_id * BITS_PER_LONG;
  if (likely(ida->ids_bmap)) {
    ptr = ida->ids_bmap + start_id;
    main_id = start_id / WORDS_PER_ITEM;
    main_bitno = start_id - main_id * WORDS_PER_ITEM;
    bit_clear(ida->main_bmap + main_id, main_bitno);
  }
  else
    ptr = ida->main_bmap + start_id;
  if (!bit_test_and_clear(ptr, bitno)) {
    kprintf(KO_WARNING "Detected an attempt to free already fried index %d "
            "[function: %s]!\n", idx, __FUNCTION__);
  }  
}

void idx_allocator_init_core(idx_allocator_t *ida)
{
  if (likely(ida->ids_bmap))
    ida->ops.alloc_id = alloc_id_large_ida;        
  else
    ida->ops.alloc_id = alloc_id_small_ida;

  ida->ops.reserve_id = reserve_id;
  ida->ops.free_id = free_id;
}

int idx_allocator_init(idx_allocator_t *ida, ulong_t idx_max)
{
  size_t bmap_sz;
  int err = -ENOMEM;
  
  bmap_sz = (round_up_pow2(idx_max) >> 3);
  ida->size = bmap_sz / sizeof(ulong_t);
  memset(ida, 0, sizeof(*ida));
  if (likely(ida->size >= MIN_IDA_SIZE)) {
    if ((bmap_sz >= PAGE_SIZE) || (bmap_sz > SLAB_OBJECT_MAX_SIZE)) {
      page_frame_t *pf = alloc_pages(bmap_sz >> PAGE_WIDTH, AF_PGEN | AF_ZERO);
      if (!pf) {
        kprintf(KO_ERROR, "Can not allocate %d pages for bitmap. ENOMEM.\n", bmap_sz >> PAGE_WIDTH);
        goto error;
      }
      
      ida->ids_bmap = pframe_to_virt(pf);
    }
    else {
      ASSERT(bmap_sz < SLAB_OBJECT_MAX_SIZE);
      ida->ids_bmap = memalloc(bmap_sz);
      if (!ida->ids_bmap) {
        kprintf(KO_ERROR "Can not allocate %d bytes for bitmap from slab. ENOMEM.\n", bmap_sz);
        goto error;
      }

      memset(ida->ids_bmap, 0, bmap_sz);
    }
  }
  else if (!ida->size)
    ida->size = sizeof(ulong_t);
  
  ida->main_bmap = memalloc(__get_main_bmap_size(ida));
  if (!ida->main_bmap) {
    kprintf(KO_ERROR, "Can not allocate %zd bytes from slab.\n", __get_main_bmap_size(ida));
    goto error;
  }
 
  memset(ida->main_bmap, 0, __get_main_bmap_size(ida));
  ida->max_id = idx_max;
  ida_allocator_init_core(ida);
  return 0;

  error:
  if (ida->ids_bmap) {
    if ((bmap_sz >= PAGE_SIZE) || (bmap_sz > SLAB_OBJECT_MAX_SIZE))
      free_pages(virt_to_pframe(ida->ids_bmap), bmap_sz >> PAGE_WIDTH);
    else
      memfree(ida->ids_bmap);
  }

  return err;
}

void idx_allocator_destroy(idx_allocator_t *ida)
{  
  if (likely(ida->ids_bmap)) {
    size_t bmap_sz = ida->size * sizeof(ulong_t);
    
    if ((bmap_sz >= PAGE_SIZE) || (bmap_sz > SLAB_OBJECT_MAX_SIZE)) {
      page_frame_t *pf = virt_to_pframe(ida->ids_bmap);    
      free_pages(pf, pages_block_size(pf));
    }
    else
      memfree(ida->ids_bmap);
  }

  memfree(ida->main_bmap);
}
