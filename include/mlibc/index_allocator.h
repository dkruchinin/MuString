#ifndef __INDEX_ALLOCATOR_H__
#define  __INDEX_ALLOCATOR_H__

#include <eza/arch/types.h>
#include <ds/list.h>

#define __IDX_ALLOCATOR_RANGE  512
#define __IDX_MAXRANGES        32
#define __IDX_MAXITEMS         (__IDX_ALLOCATOR_RANGE * __IDX_MAXRANGES)
#define __IDX_BITMAP_SIZE      (__IDX_ALLOCATOR_RANGE >> 3)

typedef struct __idx_allocator_range {
  list_node_t l;
  ulong_t range_start,values_left;
  ulong_t *bitmap;
} idx_allocator_range_t;

typedef struct __idx_allocator {
  ulong_t values_left,size;
  union {
    ulong_t *bitmap;    /* For one-entry allocators. */
    idx_allocator_range_t *entries;
  } entries;
  bool single_entry;
} idx_allocator_t;

status_t idx_allocator_initialize(idx_allocator_t *ia,ulong_t entries);
int idx_allocator_get_entry(idx_allocator_t *ia);
int idx_allocator_put_entry(idx_allocator_t *ia,int entry);

#endif
