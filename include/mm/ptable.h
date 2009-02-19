#ifndef __PTABLE_H__
#define __PTABLE_H__

#include <config.h>
#include <ds/iterator.h>
#include <mm/page.h>
#include <mm/pfalloc.h>
#include <mm/pfi.h>
#include <mm/mmpool.h>
#include <eza/arch/ptable.h>
#include <mlibc/types.h>

typedef struct __ptable_ops {
  int (*initialize_rpd)(rpd_t *rpd);
  void (*deinitialize_rpd)(rpd_t *rpd);
  void (*clone_rpd)(rpd_t *clone, rpd_t *src);
  int (*mmap)(rpd_t *rpd, uintptr_t va_from, page_idx_t npages,
              page_frame_iterator_t *pfi, ptable_flags_t flags);
  void (*munmap)(rpd_t *prd, uintptr_t va_from, page_idx_t npages);
  page_idx_t (*vaddr2page_idx)(rpd_t *rpd, uintptr_t vaddr);
  pfalloc_flags_t alloc_flags;
} ptable_ops_t;

extern ptable_ops_t ptable_ops;

#endif /* __PTABLE_H__ */
