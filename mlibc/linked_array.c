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
 * (c) Copyright 2008 Michael Tsymbalyuk <mtzaurus@gmail.com>
 *
 * mlibc/linked_array.c: Implementation of the linked array data type.
 *
 */

#include <ds/linked_array.h>
#include <mm/pfalloc.h>
#include <eza/arch/page.h>
#include <mlibc/assert.h>

int linked_array_initialize(linked_array_t *arr,ulong_t items)
{
  ulong_t item_size,array_size;

  if(!items) {
    return -1;
  }

  if(items<0x100) {
    item_size = 1;
  } else if(items < 0x10000) {
    item_size = 2;
  } else {
    item_size = sizeof(ulong_t);
  }

  array_size = items*item_size;
  /* TODO: [mt] allocate memory via slabs ! */
  arr->array = alloc_pages_addr((array_size >> PAGE_WIDTH)+1,
                                AF_PGEN );
  if(arr->array == NULL) {
    return -1;
  }

  arr->item_size = item_size;
  arr->items = items;

  linked_array_reset(arr);
  return 0;
}

void linked_array_reset(linked_array_t *arr)
{
  uint8_t *p8;
  uint16_t *p16;
  ulong_t *pl,i;

  switch(arr->item_size) {
    case 1:
      p8 = arr->array;
      break;
    case 2:
      p16 = (uint16_t*)arr->array;
      break;
    case sizeof(ulong_t):
      pl = (ulong_t*)arr->array;
      break;
    default:
      arr->head = arr->items;
      return;
  }

  for(i=1;i<arr->items;i++) {
    if(arr->item_size == 1) {
      *p8++ = i;
    } else if(arr->item_size == 2) {
      *p16++ = i;
    } else {
      *pl++ = i;
    }
  }
  arr->head = 0;
}

ulong_t linked_array_alloc_item(linked_array_t *arr)
{
  uint16_t *p16;
  ulong_t *pl,item,h;

  if(arr->head == arr->items) {
    return INVALID_ITEM_IDX;
  }

  ASSERT(arr->head < arr->items);

  item = arr->head;
  switch(arr->item_size) {
    case 1:
      h = arr->array[item];
      break;
    case 2:
      p16 = (uint16_t*)arr->array;
      h = p16[item];
      break;
    case sizeof(ulong_t):
      pl = (ulong_t*)arr->array;
      h = pl[item];
      break;
    default:
      arr->head = arr->items;
      item = INVALID_ITEM_IDX;
      break;
  }

  if(arr->head == arr->items-1) {
    arr->head=arr->items;
  } else {
    arr->head = h;
  }
  return item;
}

void linked_array_free_item(linked_array_t *arr,ulong_t item)
{
  uint16_t *p16;
  ulong_t *pl;

  /* Avoid double-frees of target item and using of deinitialized
   * linked array.
   */
  if(item == arr->head || arr->item_size == INVALID_ITEM_IDX) {
    return;
  }

  ASSERT(arr->head <= arr->items);

  switch(arr->item_size) {
    case 1:
      arr->array[item] = arr->head;
      break;
    case 2:
      p16 = (uint16_t*)arr->array;
      p16[item] = arr->head;
      break;
    case sizeof(ulong_t):
      pl = (ulong_t*)arr->array;
      pl[item] = arr->head;
      break;
    default:
      return;
  }
  arr->head = item;
}

void linked_array_deinitialize(linked_array_t *arr)
{
  arr->head = arr->items;
  arr->item_size = INVALID_ITEM_IDX;
  free_pages_addr(arr->array);
  arr->array=NULL;
}

bool linked_array_is_initialized(linked_array_t *arr)
{
  return (arr->array != NULL && arr->items != 0 &&
          arr->head <= arr->items && arr->item_size != INVALID_ITEM_IDX);
}
