#ifndef __ARCH_MM_H__
#define __ARCH_MM_H__ 

#include <ds/iterator.h>
#include <mm/page.h>
#include <eza/arch/e820map.h>
#include <eza/arch/types.h>

extern uintptr_t _kernel_end;
extern uintptr_t _kernel_start;

#define LAST_BIOS_PAGE (0x100000 >> PAGE_WIDTH)
#define KERNEL_FIRST_FREE_ADDRESS ((void *)PAGE_ALIGN(&_kernel_end))
#define KERNEL_FIRST_ADDRESS ((void *)&_kernel_start)
#define IDENT_MAP_PAGES (_mb2b(16) >> PAGE_WIDTH)

DEFINE_ITERATOR_CTX(page_frame, PF_ITER_ARCH,
                    e820memmap_t *mmap;
                    uint32_t e820id);

static inline bool is_kernel_addr(void *addr)
{
  return (((uintptr_t)addr >= (uintptr_t)(&_kernel_start)) &&
          (uintptr_t)addr <= (uintptr_t)(&_kernel_end));
}

/*static inline bool is_percpu_addr(void *addr)
{
  return ((uintptr_t)addr >= (uintptr_t)(&_percpu))
  }*/

void arch_mm_init(void);
void arch_mm_remap_pages(void);
void arch_mm_page_iter_init(page_frame_iterator_t *pfi, ITERATOR_CTX(page_frame, PF_ITER_ARCH) *ctx);
void arch_smp_mm_init(int cpu);

#endif

