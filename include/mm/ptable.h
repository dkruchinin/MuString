#ifndef __PTABLE_H__
#define __PTABLE_H__

#include <config.h>
#include <ds/iterator.h>
#include <mm/page.h>
#include <mm/pfalloc.h>
#include <mm/pfi.h>
#include <mm/mmpool.h>
#include <eza/spinlock.h>
#include <eza/arch/ptable.h>
#include <mlibc/types.h>

struct __vmm;
typedef struct __rpd {
  void *root_dir;
  spinlock_t rpd_lock;
  struct __vmm *vmm;
} rpd_t;

typedef struct __ptable_ops {
  int (*initialize_rpd)(rpd_t *rpd);
  void (*deinitialize_rpd)(rpd_t *rpd);
  void (*clone_rpd)(rpd_t *clone, rpd_t *src);
  int (*mmap)(rpd_t *rpd, uintptr_t va_from, page_idx_t npages,
              page_frame_iterator_t *pfi, ptable_flags_t flags, bool pin_pages);
  void (*munmap)(rpd_t *prd, uintptr_t va_from, page_idx_t npages, bool unpin_pages);
  int (*mmap_one_page)(rpd_t *rpd, uintptr_t vaddr, page_idx_t pidx, ptable_flags_t flags);
  void (*munmap_one_page)(rpd_t *rpd, uintptr_t vaddr);
  page_idx_t (*vaddr2page_idx)(rpd_t *rpd, uintptr_t vaddr, /* OUT */ pde_t **retpde);
  pfalloc_flags_t alloc_flags;
} ptable_ops_t;

#define pagetable_lock(rpd) spinlock_lock(&(rpd)->rpd_lock)
#define pagetable_unlock(rpd) spinlock_unlock(&(rpd)->rpd_lock)

extern ptable_ops_t ptable_ops;

static inline int initialize_rpd(rpd_t *rpd, struct __vmm *vmm)
{
  spinlock_initialize(&rpd->rpd_lock);
  rpd->vmm = vmm;
  return ptable_ops.initialize_rpd(rpd);
}

static inline void deinitialize_rpd(rpd_t *rpd)
{
  ptable_ops.deinitialize_rpd(rpd);
}

#endif /* __PTABLE_H__ */
