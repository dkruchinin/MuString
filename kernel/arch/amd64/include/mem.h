#ifndef __MSTRING_ARCH_MEM_H__
#define __MSTRING_ARCH_MEM_H__

#include <config.h>
#include <mm/page.h>
#include <arch/page.h>
#include <arch/init.h>
#include <mstring/stddef.h>
#include <mstring/types.h>

#define ARCH_NUM_MMPOOLS 2

#define MIN_MEM_REQUIRED    MB2B(4)
#define MAX_PAGES_MAP_FIRST (MB2B(512) >> PAGE_WIDTH)
#define IDENT_MAP_PAGES     (MB2B(2) >> PAGE_WIDTH)
#define KERNEL_END_VIRT     ((uintptr_t)__kernel_end)
#define KERNEL_END_PHYS     KVIRT_TO_PHYS(KERNEL_END_VIRT)
#define ARCH_NUM_MMPOOLS    2
#define USPACE_VADDR_TOP    (16UL << 40UL) /* 16 terabytes */
#define USPACE_VADDR_BOTTOM 0x1001000UL

extern uintptr_t __kernel_end;
struct __vmm; /* FIXME DK: remove after cleanup */

INITCODE void arch_mem_init(void);
INITCODE void arch_cpu_enable_paging(void);
INITCODE void arch_register_mmpools(void);
INITCODE void arch_configure_mmpools(void);

void *arch_root_pdir_allocate_ctx(void);
void arch_root_pdir_free_ctx(void *ctx);
void map_kernel_area(struct __vmm *vmm);
void arch_smp_mm_init(cpu_id_t cpu); /* FIXME DK: remove after cleanup */
uintptr_t __allocate_vregion(page_idx_t num_pages);

extern void  __userspace_trampoline_codepage(void);

#endif /* __MSTRING_ARCH_MEM_H__ */
