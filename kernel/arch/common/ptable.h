#ifndef __GENARCH_PTABLE_H__
#define __GENARCH_PTABLE_H__

#include <ds/iterator.h>
#include <mm/page.h>
#include <mm/mem.h>
#include <arch/ptable.h>
#include <mstring/types.h>

int generic_ptable_map(rpd_t *rpd, uintptr_t va_from, page_idx_t npages,
                       page_frame_iterator_t *pfi, ptable_flags_t flags, bool pin_pages);
void generic_ptable_unmap(rpd_t *rpd, uintptr_t va_from, page_idx_t npages, bool unpin_pages);
page_idx_t generic_vaddr2page_idx(rpd_t *rpd, uintptr_t vaddr, pde_t **pde);
page_frame_t *generic_create_pagedir(void);
int generic_map_page(rpd_t *rpd, uintptr_t addr, page_idx_t pidx, ptable_flags_t flags);
void generic_unmap_page(rpd_t *rpd, uintptr_t addr);

#endif /* __GENARCH_PTABLE_H__ */
