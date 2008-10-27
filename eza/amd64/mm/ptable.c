#include <ds/list.h>
#include <mlibc/assert.h>
#include <mm/page.h>
#include <mm/pfalloc.h>
#include <mm/mmap.h>
#include <eza/spinlock.h>
#include <eza/arch/ptable.h>
#include <eza/arch/types.h>

pde_flags_t pgt_translate_flags(unsigned int mmap_flags)
{
  pde_flags_t flags = 0;

  CT_ASSERT(sizeof(mmap_flags) >= sizeof(mmap_flags_t));
  if (mmap_flags & MAP_USER)
    flags |= PDE_US;
  if (mmap_flags & MAP_WRITE)
    flags |= PDE_RW;
  if (mmap_flags & MAP_DONTCACHE)
    flags |= PDE_PCD;
  if (!(mmap_flags & MAP_EXEC))
    flags |= PDE_NX;

  return flags;
}

page_frame_t *pgt_create_pagedir(page_frame_t *parent, pdir_level_t level)
{
  page_frame_t *dir = alloc_page(AF_ZERO | AF_PGEN);
  
  if (dir)
    mm_pagedir_initialize(dir, parent, level);

  return dir;
}

