#include <mlibc/index_allocator.h>
#include <eza/arch/types.h>
#include <ds/list.h>
#include <mm/slab.h>
#include <eza/errno.h>
#include <eza/bits.h>

#define __LONGS_PER_BITMAP  (__IDX_BITMAP_SIZE/sizeof(ulong_t))

static void __initialize_bitmap(ulong_t *bitmap)
{
  int i;

  for(i=0;i<__LONGS_PER_BITMAP;i++) {
    *bitmap++=~(ulong_t)0;
  }
}

status_t idx_allocator_initialize(idx_allocator_t *ia,ulong_t entries)
{
  status_t r=-EINVAL;

  if( entries <= __IDX_MAXITEMS ) {
    ia->single_entry=(entries <= __IDX_ALLOCATOR_RANGE);
    ia->values_left=entries;
    ia->size=entries;

    if( ia->single_entry ) {
      ia->entries.bitmap=memalloc(__IDX_BITMAP_SIZE);
      if( ia->entries.bitmap ) {
        __initialize_bitmap(ia->entries.bitmap);
        r=0;
      } else {
        r=-ENOMEM;
      }
    } else {
      return -EINVAL;
    }
  }
  return r;
}

int idx_allocator_get_entry(idx_allocator_t *ia)
{
  int idx=-1;

  if( ia->values_left ) {
    if( ia->single_entry ) {
      idx=find_first_bit_mem(ia->entries.bitmap,__LONGS_PER_BITMAP);
      if( idx == INVALID_BIT_INDEX ) {
        idx=-1;
      } else {
        reset_and_test_bit_mem(ia->entries.bitmap,idx);
        ia->values_left--;
      }
    }
  }
  return idx;
}

int idx_allocator_put_entry(idx_allocator_t *ia,int entry)
{
  if( entry >=0 && entry < ia->size ) {
    if( ia->single_entry ) {
      if( set_and_test_bit_mem(ia->entries.bitmap,entry) ) {
        kprintf(KO_WARNING "idx_allocator_put_entry(): Freeing insufficient entry %d\n",
                entry);
      } else {
        ia->values_left++;
        return 0;
      }
    }
  }
  return -EINVAL;
}
