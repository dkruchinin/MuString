#include <ds/iterator.h>
#include <ds/list.h>
#include <mlibc/stddef.h>
#include <mm/mm.h>
#include <mm/page.h>
#include <mm/mmap.h>
#include <mm/pfalloc.h>
#include <eza/spinlock.h>
#include <eza/errno.h>
#include <eza/arch/page.h>
#include <eza/arch/ptable.h>
#include <eza/arch/types.h>

#define DEFAULT_MAPDIR_FLAGS (MAP_RW | MAP_EXEC)

page_frame_t *kernel_root_pagedir = NULL;

page_idx_t __mm_pin_virt_addr(page_frame_t *dir, uintptr_t va, pdir_level_t level)
{
  pde_t *pde = pgt_fetch_entry(dir, pgt_vaddr2idx(va, level));

  if (!pgt_pde_is_mapped(pde))
    return -1;
  if (level == PTABLE_LEVEL_FIRST)
    return pgt_pde_page_idx(pde);

  return __mm_pin_virt_addr(pgt_get_pde_subdir(pde), va, level - 1);
}

void mm_pagedir_initialize(page_frame_t *new_dir, page_frame_t *parent, pdir_level_t level)
{
  new_dir->entries = 0;  
  new_dir->level = level;
  list_init_head(&new_dir->head);
  if (likely(parent != NULL))
    list_add2tail(&parent->head, &new_dir->node);
}

int mm_populate_pagedir(pde_t *pde, pde_flags_t flags)
{
  page_frame_t *dir = pgt_get_pde_dir(pde);
  page_frame_t *subdir = pgt_create_pagedir(dir, dir->level - 1);

  if (!subdir)
    return -ENOMEM;

  ASSERT(subdir->level >= PTABLE_LEVEL_FIRST);
  ASSERT((dir->level <= PTABLE_LEVEL_LAST) ||
         (dir->level > PTABLE_LEVEL_FIRST));

  pgt_pde_save(pde, pframe_number(subdir), flags);
  dir->entries++;
  ASSERT(dir->entries <= PTABLE_DIR_ENTRIES);

  return 0;
}

int mm_map_entries(pde_t *pde_start, pde_idx_t entries,
                   page_frame_iterator_t *pfi, pde_flags_t flags)
{
  page_frame_t *dir = pgt_get_pde_dir(pde_start);
  pde_t *pde = pde_start;  

  ASSERT(dir->level == PTABLE_LEVEL_FIRST);
  dir->entries += entries;
  ASSERT(dir->entries <= PTABLE_DIR_ENTRIES);
  while (entries--) {
    if (pgt_pde_is_mapped(pde)) {
      return -EINVAL;
    }

    iter_next(pfi);
    if (!iter_isrunning(pfi)) {
      return -EINVAL;
    }

    pgt_pde_save(pde++, pfi->pf_idx, flags);
  }
    
  return 0;
}

int mmap(page_frame_t *root_dir, uintptr_t va, page_idx_t first_page, int npages, mmap_flags_t flags)
{  
  page_idx_t idx;
  mmap_info_t minfo;
  ITERATOR_CTX(page_frame, PF_ITER_INDEX) pfi_index_ctx;

  idx = virt_to_pframe_id((void *)va);
  minfo.va_from = va;
  minfo.va_to = va + (npages << PAGE_WIDTH);
  minfo.flags = flags;
  mm_init_pfiter_index(&minfo.pfi, &pfi_index_ctx, first_page, first_page + npages - 1);
  
  return mmap_pages(root_dir, &minfo);
}

int __mmap_pages(page_frame_t *dir, mmap_info_t *minfo, pdir_level_t level)
{
  pde_idx_t idx, entries;
  uintptr_t range = pgt_get_range(level);
  int ret = 0;  
  
  idx = pgt_vaddr2idx(minfo->va_from, level);
  if (likely((minfo->va_to - minfo->va_from) >= range))
    entries = PTABLE_DIR_ENTRIES - idx;
  else
    entries = pgt_vaddr2idx(minfo->va_to - minfo->va_from, level) + 1;

  if (level == PTABLE_LEVEL_FIRST) {
    ret = mm_map_entries(pgt_fetch_entry(dir, idx), entries, &minfo->pfi, pgt_translate_flags(minfo->flags));
    minfo->va_from += (range - pgt_idx2vaddr(idx, level));
  }
  else {
    pde_flags_t _flags = pgt_translate_flags(DEFAULT_MAPDIR_FLAGS);
    
    do {
      pde_t *pde = pgt_fetch_entry(dir, idx++);

      if (!pgt_pde_is_mapped(pde)) {
        ret = mm_populate_pagedir(pde, _flags);
        if (ret < 0)
          return ret;
      }
      
      ret = __mmap_pages(pgt_get_pde_subdir(pde), minfo, level - 1);
      if (ret < 0)
        return ret;            
    } while (--entries > 0);
  }

  return ret;
}
