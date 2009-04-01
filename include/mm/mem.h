#ifndef __MEM_H__
#define __MEM_H__

#include <config.h>
#include <mm/page.h>
#include <mm/pfalloc.h>
#include <mm/mmpool.h>
#include <eza/spinlock.h>
#include <eza/arch/mm.h>
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
  int (*mmap_one_page)(rpd_t *rpd, uintptr_t vaddr,
                       page_idx_t pidx, ptable_flags_t flags);
  void (*munmap_one_page)(rpd_t *rpd, uintptr_t vaddr);
  page_idx_t (*vaddr2page_idx)(rpd_t *rpd, uintptr_t vaddr,
                               /* OUT */ pde_t **retpde);
  pfalloc_flags_t alloc_flags;
} ptable_ops_t;

extern ptable_ops_t ptable_ops;

#define pagetable_lock(rpd) spinlock_lock(&(rpd)->rpd_lock)
#define pagetable_unlock(rpd) spinlock_unlock(&(rpd)->rpd_lock)

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

static inline int mmap_one_page(rpd_t *rpd, uintptr_t va,
                                page_idx_t pidx, ulong_t flags)
{
  return ptable_ops.mmap_one_page(rpd, va, pidx, kmap_to_ptable_flags(flags));
}

static inline void munmap_one_page(rpd_t *rpd, uintptr_t va)
{
  ptable_ops.munmap_one_page(rpd, va);
}


static inline page_idx_t __vaddr2page_idx(rpd_t *rpd,
                                          uintptr_t addr, pde_t **pde)
{
  return ptable_ops.vaddr2page_idx(rpd, addr, pde);
}

static inline page_idx_t vaddr2page_idx(rpd_t *rpd, uintptr_t addr)
{
  return __vaddr2page_idx(rpd, addr, NULL);
}

static inline bool page_is_mapped(rpd_t *rpd, uintptr_t va)
{
  return (vaddr2page_idx(rpd, va) != PAGE_IDX_INVAL);
}

static inline void unpin_page_frame(page_frame_t *pf)
{
  if (atomic_dec_and_test(&pf->refcount))
    free_page(pf);
}

static inline void pin_page_frame(page_frame_t *pf)
{
  atomic_inc(&pf->refcount);
}

#define valid_user_address(va)                  \
  valid_user_address_range(va, 0)

static inline bool valid_user_address_range(uintptr_t va_start,
                                            uintptr_t length)
{
    return ((va_start >= USPACE_VA_BOTTOM) &&
            ((va_start + length) <= USPACE_VA_TOP));
}

static inline void *user_to_kernel_vaddr(rpd_t *rpd, uintptr_t addr)
{
  page_idx_t idx = ptable_ops.vaddr2page_idx(rpd, addr, NULL);

  if (idx == PAGE_IDX_INVAL)
    return NULL;

  return ((char *)pframe_to_virt(pframe_by_number(idx)) +
          (addr - PAGE_ALIGN_DOWN(addr)));
}

#define lock_page_frame(p, bit)                 \
  spinlock_lock_bit(&(p)->flags, bitnumber(bit))
#define unlock_page_frame(p, bit)               \
  spinlock_unlock_bit(&(p)->flags, bitnumber(bit))

extern rpd_t kernel_rpd;
struct __vmrange;

int mmap_kern(uintptr_t va_from, page_idx_t first_page, pgoff_t npages, long flags);
int handle_copy_on_write(struct __vmrange *vmr, uintptr_t addr,
                         page_frame_t *dst_page, page_frame_t *src_page);
#endif /* __MEM_H__ */
