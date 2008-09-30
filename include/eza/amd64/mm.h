#ifndef __ARCH_MM_H__
#define __ARCH_MM_H__ 

#include <ds/iterator.h>
#include <mm/page.h>
#include <eza/arch/e820map.h>
#include <eza/arch/boot.h>
#include <eza/arch/types.h>

extern int _kernel_end;
extern int _kernel_start;
extern int _low_kernel_end;
extern uintptr_t _kernel_extended_end;

#define LAST_BIOS_PAGE (BIOS_END_ADDR >> PAGE_WIDTH)
#define KERNEL_FIRST_FREE_ADDRESS ((void *)PAGE_ALIGN(_kernel_extended_end))
#define KERNEL_FIRST_ADDRESS ((void *)&_kernel_start)
#define IDENT_MAP_PAGES (_mb2b(16) >> PAGE_WIDTH)

DEFINE_ITERATOR_CTX(page_frame, PF_ITER_ARCH,
                    e820memmap_t *mmap;
                    uint32_t e820id);

static inline bool is_kernel_addr(void *a)
{
  uintptr_t addr = (uintptr_t)a;

  if (addr >= (uintptr_t)&_kernel_start) {
    return (addr <= _kernel_extended_end);
  }
  return ((k2p(addr) >= (uintptr_t)(BOOT_OFFSET - AP_BOOT_OFFSET)) &&
          (addr < (uintptr_t)&_kernel_start));
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

