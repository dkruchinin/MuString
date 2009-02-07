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
 * (c) Copyright 2008 Michael Tsymbalyuk <mtzaurus@gmail.com>
 *
 * mlibc/index_array.c: routines for the index array implementation.
 */


#include <mlibc/stddef.h>
#include <mlibc/index_array.h>
#include <eza/arch/types.h>
#include <ds/list.h>
#include <mm/page.h>
#include <mm/pfalloc.h>
#include <mlibc/kprintf.h>

static index_array_entry_t *allocate_entries(range_type_t num_entries)
{
  static index_array_entry_t entries[128];

  /* TODO: [mt] Add dynamic entry array allocation. */
  return &entries[0];
}


bool index_array_initialize(index_array_t *array, range_type_t range)
{
  if( array != NULL && (range & IA_SIZE_MASK) == 0 &&
      range >= PAGE_SIZE && range <= IA_MAXRANGE ) {
      range_type_t item, i, item_count;

      array->values_left = range;
      array->max_value = range-1;
      list_init_head(&array->full_entries);
      list_init_head(&array->free_entries);

      item_count = range/IA_ENTRY_RANGE;

      array->entries = allocate_entries(item_count);
      if(array->entries != NULL) {
        index_array_entry_t *entry;

        for(item = 0;item < item_count; item++) {
          char *area;

          if( item % IA_ENTRIES_PER_PAGE == 0 ) {
            uint64_t *p64;
            page_frame_t *page = alloc_page(AF_PGEN);
            area = pframe_to_virt(page);;

            if(area == NULL) {
              return false;
            }

            /* Initialize the whole page. */
            p64 = (uint64_t*)area;
            for(i=0; i< PAGE_SIZE/(IA_ITEM_GRANULARITY/8); i++) {
              *p64++ = IA_BITMAP_INIT_PATTERN;
            }
          }

          entry = &array->entries[item];
          entry->id = item;
          entry->values_left = IA_ENTRY_RANGE;
          entry->range_start = IA_ENTRY_RANGE * item;

          list_init_node(&entry->l);

          /* Now initialize the bitmap. */
          entry->bitmap = (uint64_t *)(area + (item % IA_ENTRIES_PER_PAGE) * IA_ENTRY_RANGE);
        }

        /* OK, now build initial list of initialized entries. */
        entry = array->entries;
        for(item = 0; item < item_count; item++) {
            list_add2tail(&array->free_entries, &entry->l);
          entry++;
        }
      }
      return true;
    }
  return false;
}

void index_array_deinitialize(index_array_t *array)
{
  array->entries = NULL;
  array->values_left = 0;
}

range_type_t index_array_alloc_value(index_array_t *array)
{
  if(array->values_left > 0) {
    register index_array_entry_t *entry = list_entry(list_node_first(&array->free_entries),index_array_entry_t,l);

    bit_idx_t idx = find_first_bit_mem(entry->bitmap,IA_ENTRY_RANGE / 64);

    array->values_left--;
    entry->values_left--;

    /* Remove this item from the list in case it doesn't have any free values. */
    if(entry->values_left == 0) {
      list_del(&entry->l);
      list_add2head(&array->full_entries,&entry->l);
    }

    if(idx != INVALID_BIT_INDEX) {
      reset_and_test_bit_mem(entry->bitmap,idx);
      return entry->range_start + idx;
    }
  }
  return IA_INVALID_VALUE;
}

bool index_array_free_value(index_array_t *array,range_type_t value)
{
  if( array->entries != NULL && value <= array->max_value) {
    register range_type_t idx = value >> IA_ENTRY_SHIFT;
    register range_type_t off = value & (IA_ENTRY_RANGE-1);

    index_array_entry_t *entry = &array->entries[idx];
    if( set_and_test_bit_mem(entry->bitmap,off) != 0 ) {
      kprintf( KO_WARNING "index_array_free_value(): Freeing insufficient index: %d\n", value );
    } else {
      entry->values_left++;
      array->values_left++;

      /* Return entry that has just become available (0 -> 1 free entries)
       * to the list ov available entries.
       */
      if(entry->values_left == 1) {
        list_del(&entry->l);
        /* We add this 'fresh entry' to the end of list to prevent it from putting
         * back to the array of free entries after the next allocation.
         */
        list_add2tail(&array->free_entries,&entry->l);
      }
      return true;
    }
  }

  return false;
}

