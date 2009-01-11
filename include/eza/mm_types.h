#ifndef __ARCH_MM_TYPES_H__
#define __ARCH_MM_TYPES_H__

#include <ds/iterator.h>
#include <mm/page.h>
#include <mlibc/types.h>
#include <eza/arch/e820map.h>

/**
 * @struct rpd_t
 * @brief Root page directory structures (AMD64-specific)
 */
typedef struct __rpd {
  page_frame_t *pml4;
} rpd_t;

/***
 * page frame iterators
 */

DEFINE_ITERATOR_CTX(page_frame, PF_ITER_ARCH,
                    e820memmap_t *mmap;
                    uint32_t e820id);

DEFINE_ITERATOR_CTX(page_frame, PF_ITER_PTABLE,
                    rpd_t *rpd;
                    uintptr_t va_from;
                    uintptr_t va_cur;
                    uintptr_t va_to,
                    int pde_level);

#endif /* __ARCH_MM_TYPES_H__ */
