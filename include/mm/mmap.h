#ifndef __MMAP_H__
#define __MMAP_H__

#include <mlibc/string.h>
#include <mm/page.h>
#include <eza/arch/mm.h>
#include <eza/arch/ptable.h>
#include <eza/arch/types.h>

/**
 * @typedef uint8_t mmap_flags_t
 * Memory mapping flags.
 */
typedef uint8_t mmap_flags_t;

/* flags for memory mapping */
#define MAP_READ      0x01 /**< Mapped page may be readed */
#define MAP_WRITE     0x02 /**< Mapped page may be written */
#define MAP_RW        0x03 /**< Mapped page may be both readed and written */
#define MAP_USER      0x04 /**< Mapped page is visible for user */
#define MAP_EXEC      0x08 /**< Mapped page may be executed */
#define MAP_DONTCACHE 0x10 /**< Prevent caching of mapped page */

extern page_frame_t *kernel_root_pagedir;
extern bool map_verbose;

typedef struct __mmap_info {
  page_frame_iterator_t pfi;
  uintptr_t va_from;
  uintptr_t va_to;
  mmap_flags_t flags;
} mmap_info_t;

#define mmap_pages(root_dir, minfo)             \
  __mmap_pages(root_dir, minfo, PTABLE_LEVEL_LAST)
#define mmap_kern_pages(minfo)                  \
  mmap_pages(kernel_root_pagedir, minfo)
#define mmap_kern(va, first_page, npages, flags)            \
  mmap(kernel_root_pagedir, va, first_page, npages, flags)
#define mm_pin_virt_addr(root_dir, va)                 \
  __mm_pin_virt_addr(root_dir, (uintptr_t)(va), PTABLE_LEVEL_LAST)
#define mm_virt_addr_is_mapped(root_dir, va)       \
  (mm_pin_virt_addr(root_dir, (uintptr_t)(va)) >= 0)
#define mm_create_root_pagedir()                \
  pgt_create_pagedir(NULL, PTABLE_LEVEL_LAST)

int __mmap_pages(page_frame_t *dir, mmap_info_t *minfo, pdir_level_t level);
int mmap(page_frame_t *root_dir, uintptr_t va, page_idx_t first_page, int npages, mmap_flags_t flags);
int mm_populate_pagedir(pde_t *pde, pde_flags_t flags);
int mm_map_entries(pde_t *pde_start, pde_idx_t entries,
                   page_frame_iterator_t *pfi, pde_flags_t flags);
page_idx_t __mm_pin_virt_addr(page_frame_t *dir, uintptr_t va, pdir_level_t level);
void mm_pagedir_initialize(page_frame_t *new_dir, page_frame_t *parent, pdir_level_t level);

#endif /* __MMAP_H__ */
